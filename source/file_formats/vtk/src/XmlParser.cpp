// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DebugCodes.h"
#include "DisablePXRWarnings.h"
#include "FileFormatError.h"
#include "Parser.h"
#include "ParserUtils.h"
#include "pugixml.hpp"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace cae::vtk
{

namespace
{

constexpr std::string_view AppendedDataCloseTag = "</AppendedData>";
constexpr std::string_view SyntheticAttributePrefix = "__cae_";

constexpr const char* PayloadStartOffsetAttr = "__cae_payload_start_offset";
constexpr const char* PayloadEndOffsetAttr = "__cae_payload_end_offset";
constexpr const char* PayloadStorageByteCountAttr = "__cae_payload_storage_byte_count";
constexpr const char* PayloadFirstSegmentByteCountAttr = "__cae_payload_first_segment_byte_count";
constexpr const char* PayloadSecondSegmentByteCountAttr = "__cae_payload_second_segment_byte_count";
constexpr const char* AsciiValueCountAttr = "__cae_ascii_value_count";
constexpr const char* BinaryDecodedByteCountAttr = "__cae_binary_decoded_byte_count";
constexpr const char* AppendedDataOffsetAttr = "__cae_appended_data_offset";
constexpr const char* AppendedEncodedByteCountAttr = "__cae_appended_encoded_byte_count";

struct XmlEnvelope
{
    bool hasAppendedData = false;
    bool appendedEncodingRaw = false;
    uint64_t appendedDataOffset = 0;
    uint64_t appendedEncodedByteCount = 0;
};

struct InlineTextRange
{
    uint64_t offset = 0;
    size_t byteCount = 0;
};

struct XmlParseContext
{
    std::string filePath;

    ByteOrder byteOrder = ByteOrder::LittleEndian;
    XmlHeaderType headerType = XmlHeaderType::UInt32;
    XmlCompressor compressor = XmlCompressor::None;

    bool hasAppendedData = false;
    bool appendedEncodingRaw = false;
    uint64_t appendedDataOffset = 0;
    uint64_t appendedEncodedByteCount = 0;
};

struct ParsedXmlAttribute
{
    std::string name;
    std::string value;
};

size_t CheckedAddSize(size_t lhs, size_t rhs)
{
    if (rhs > std::numeric_limits<size_t>::max() - lhs)
        throw std::overflow_error("VTK XML metadata count overflow");
    return lhs + rhs;
}

size_t CheckedSize(uint64_t value, const char* context)
{
    if (value > std::numeric_limits<size_t>::max())
        throw std::overflow_error(context);
    return value;
}

ByteOrder ParseXmlByteOrder(const std::string& value);
XmlHeaderType ParseXmlHeaderType(const std::string& value);
XmlCompressor ParseXmlCompressor(const std::string& value);
size_t XmlHeaderWordSize(XmlHeaderType headerType);
uint64_t ReadXmlEndianUnsigned(const unsigned char* bytes, size_t byteCount, ByteOrder byteOrder);
int Base64DecodeValue(unsigned char c);
bool IsXmlWhitespace(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool IsXmlSpaceFreeNameChar(char c)
{
    return !IsXmlWhitespace(c) && c != '=' && c != '/' && c != '>';
}

std::string DecodeXmlEntities(std::string_view value)
{
    const size_t firstEntity = value.find('&');
    if (firstEntity == std::string_view::npos)
        return std::string(value);

    std::string decoded;
    decoded.reserve(value.size());
    decoded.append(value.data(), firstEntity);

    size_t cursor = firstEntity;
    while (cursor < value.size())
    {
        if (value[cursor] != '&')
        {
            decoded.push_back(value[cursor++]);
            continue;
        }

        const size_t semi = value.find(';', cursor + 1);
        if (semi == std::string_view::npos)
        {
            decoded.push_back(value[cursor++]);
            continue;
        }

        const std::string_view entity = value.substr(cursor + 1, semi - cursor - 1);
        if (entity == "amp")
            decoded.push_back('&');
        else if (entity == "lt")
            decoded.push_back('<');
        else if (entity == "gt")
            decoded.push_back('>');
        else if (entity == "quot")
            decoded.push_back('"');
        else if (entity == "apos")
            decoded.push_back('\'');
        else
        {
            decoded.push_back('&');
            decoded.append(entity.data(), entity.size());
            decoded.push_back(';');
        }
        cursor = semi + 1;
    }
    return decoded;
}

std::string TrimXmlWhitespace(std::string_view text)
{
    size_t begin = 0;
    while (begin < text.size() && IsXmlWhitespace(text[begin]))
        ++begin;
    size_t end = text.size();
    while (end > begin && IsXmlWhitespace(text[end - 1]))
        --end;
    return std::string(text.substr(begin, end - begin));
}

std::vector<ParsedXmlAttribute> ParseXmlAttributes(std::string_view body, size_t cursor)
{
    std::vector<ParsedXmlAttribute> attributes;
    while (true)
    {
        while (cursor < body.size() && IsXmlWhitespace(body[cursor]))
            ++cursor;
        if (cursor >= body.size())
            break;

        const size_t nameBegin = cursor;
        while (cursor < body.size() && IsXmlSpaceFreeNameChar(body[cursor]))
            ++cursor;
        if (cursor == nameBegin)
            throw cae::FileFormatError("VTK XML attribute is missing a name");

        ParsedXmlAttribute attr;
        attr.name = std::string(body.substr(nameBegin, cursor - nameBegin));
        if (attr.name.rfind(std::string(SyntheticAttributePrefix), 0) == 0)
            throw cae::FileFormatError("VTK XML source attribute uses reserved parser prefix: " + attr.name);

        while (cursor < body.size() && IsXmlWhitespace(body[cursor]))
            ++cursor;
        if (cursor >= body.size() || body[cursor] != '=')
        {
            attr.value.clear();
            attributes.push_back(std::move(attr));
            continue;
        }
        ++cursor;

        while (cursor < body.size() && IsXmlWhitespace(body[cursor]))
            ++cursor;
        if (cursor >= body.size() || (body[cursor] != '"' && body[cursor] != '\''))
            throw cae::FileFormatError("VTK XML attribute value must be quoted");

        const char quote = body[cursor++];
        const size_t valueBegin = cursor;
        while (cursor < body.size() && body[cursor] != quote)
            ++cursor;
        if (cursor >= body.size())
            throw cae::FileFormatError("VTK XML attribute value is unterminated");

        attr.value = DecodeXmlEntities(body.substr(valueBegin, cursor - valueBegin));
        attributes.push_back(std::move(attr));
        ++cursor;
    }
    return attributes;
}

std::string XmlAttribute(const pugi::xml_node& node, const char* name, std::string_view fallback = {})
{
    if (!node)
        return std::string(fallback);
    pugi::xml_attribute attr = node.attribute(name);
    return attr ? attr.as_string() : std::string(fallback);
}

uint64_t XmlUnsignedAttribute(const pugi::xml_node& node, const char* name, uint64_t fallback = 0)
{
    const std::string value = XmlAttribute(node, name);
    if (value.empty())
        return fallback;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (end != value.c_str() + value.size())
        throw cae::FileFormatError("Failed to parse VTK XML unsigned attribute: " + value);
    return static_cast<uint64_t>(parsed);
}

struct ParsedXmlTag
{
    std::string name;
    std::vector<ParsedXmlAttribute> attributes;
    bool selfClosing = false;
};

ParsedXmlTag ParseXmlStartTag(std::string body)
{
    body = TrimXmlWhitespace(body);
    ParsedXmlTag tag;
    if (!body.empty() && body.back() == '/')
    {
        tag.selfClosing = true;
        body.pop_back();
        body = TrimXmlWhitespace(body);
    }

    size_t cursor = 0;
    while (cursor < body.size() && !IsXmlWhitespace(body[cursor]))
        ++cursor;
    if (cursor == 0)
        throw cae::FileFormatError("VTK XML start tag is missing a name");

    tag.name = std::string(body.substr(0, cursor));
    tag.attributes = ParseXmlAttributes(body, cursor);
    return tag;
}

std::string ParseXmlCloseTagName(std::string body)
{
    body = TrimXmlWhitespace(body);
    if (body.empty() || body.front() != '/')
        throw cae::FileFormatError("VTK XML close tag is malformed");
    body.erase(body.begin());
    body = TrimXmlWhitespace(body);
    size_t end = 0;
    while (end < body.size() && !IsXmlWhitespace(body[end]))
        ++end;
    return std::string(body.substr(0, end));
}

class StreamingXmlReader
{
public:
    /// Opens a VTK XML file as a byte-addressable stream.
    ///
    /// The tokenizer needs absolute file offsets for lazy payload reads, so this
    /// class keeps its own logical position and uses explicit seeks instead of
    /// relying on formatted stream extraction.
    explicit StreamingXmlReader(std::string filePath) : _filePath(std::move(filePath))
    {
        _input.open(_filePath, std::ios::binary);
        if (!_input)
            throw cae::FileFormatError("Failed to open VTK XML file: " + _filePath);

        _input.seekg(0, std::ios::end);
        const std::streamoff end = _input.tellg();
        if (end < 0)
            throw cae::FileFormatError("Failed to stat VTK XML file: " + _filePath);
        _fileSize = static_cast<uint64_t>(end);
        Seek(0);
    }

    uint64_t FileSize() const
    {
        return _fileSize;
    }

    uint64_t Position() const
    {
        return _position;
    }

    /// Read one byte and advance the absolute file offset.
    int Get()
    {
        char c = 0;
        _input.read(&c, 1);
        if (_input.gcount() != 1)
            return EOF;
        ++_position;
        return static_cast<unsigned char>(c);
    }

    /// Return the next byte without committing to it.
    int Peek()
    {
        const uint64_t saved = _position;
        const int c = Get();
        Seek(saved);
        return c;
    }

    /// Seek to an absolute byte offset in the source file.
    void Seek(uint64_t offset)
    {
        if (offset > _fileSize)
            throw cae::FileFormatError("VTK XML seek offset is outside the file: " + _filePath);
        _input.clear();
        SeekAbsolute(_input, offset);
        _position = offset;
    }

    /// Read up to `byteCount` raw bytes into `target`.
    size_t Read(char* target, size_t byteCount)
    {
        _input.read(target, static_cast<std::streamsize>(byteCount));
        const auto got = static_cast<size_t>(_input.gcount());
        _position += got;
        return got;
    }

    /// Return true and leave the stream at the current `<` when one is found.
    bool SeekToChar(char needle)
    {
        std::array<char, 64u << 10> buffer{};
        while (_position < _fileSize)
        {
            const uint64_t chunkBegin = _position;
            const size_t got = Read(buffer.data(), std::min<uint64_t>(buffer.size(), _fileSize - _position));
            if (got == 0)
                return false;

            const char* first = buffer.data();
            const char* last = first + got;
            const auto found = std::find(first, last, needle);
            if (found != last)
            {
                Seek(chunkBegin + static_cast<uint64_t>(found - first));
                return true;
            }
        }
        return false;
    }

    /// Consume `text` if it appears at the current position.
    bool Consume(std::string_view text)
    {
        const uint64_t saved = _position;
        for (char expected : text)
        {
            const int c = Get();
            if (c == EOF || static_cast<char>(c) != expected)
            {
                Seek(saved);
                return false;
            }
        }
        return true;
    }

    bool StartsWith(std::string_view text)
    {
        const uint64_t saved = _position;
        const bool matched = Consume(text);
        Seek(saved);
        return matched;
    }

    /// Read from after `<` up to the matching tag `>`, respecting quoted
    /// attribute values. This is enough XML syntax for VTK metadata tags.
    std::string ReadTagBody()
    {
        std::string body;
        body.reserve(256);
        char quote = '\0';
        while (true)
        {
            const int next = Get();
            if (next == EOF)
                throw cae::FileFormatError("VTK XML tag is unterminated: " + _filePath);
            const auto c = static_cast<char>(next);
            if (quote != '\0')
            {
                if (c == quote)
                    quote = '\0';
                body.push_back(c);
                continue;
            }
            if (c == '"' || c == '\'')
            {
                quote = c;
                body.push_back(c);
                continue;
            }
            if (c == '>')
                return body;
            body.push_back(c);
            if (body.size() > (1u << 20))
                throw cae::FileFormatError("VTK XML metadata tag exceeds parser safety limit: " + _filePath);
        }
    }

    /// Skip a processing instruction, comment, CDATA section, or declaration.
    void SkipUntil(std::string_view delimiter)
    {
        size_t matched = 0;
        while (true)
        {
            const int next = Get();
            if (next == EOF)
                throw cae::FileFormatError("VTK XML construct is unterminated: " + _filePath);
            const auto c = static_cast<char>(next);
            if (c == delimiter[matched])
            {
                ++matched;
                if (matched == delimiter.size())
                    return;
            }
            else
            {
                matched = c == delimiter[0] ? 1 : 0;
            }
        }
    }

    /// Skip XML whitespace and return the first non-whitespace byte offset.
    uint64_t SkipWhitespace()
    {
        while (true)
        {
            const int c = Peek();
            if (c == EOF || !IsXmlWhitespace(static_cast<char>(c)))
                return _position;
            Get();
        }
    }

    /// Read a bounded file range while preserving the current stream position.
    std::string ReadRange(uint64_t offset, size_t byteCount)
    {
        const uint64_t saved = _position;
        Seek(offset);
        std::string text(byteCount, '\0');
        const size_t got = Read(text.data(), text.size());
        if (got != byteCount)
            throw cae::FileFormatError("Failed to read VTK XML metadata byte range: " + _filePath);
        Seek(saved);
        return text;
    }

private:
    std::string _filePath;
    std::ifstream _input;
    uint64_t _fileSize = 0;
    uint64_t _position = 0;
};

size_t Base64EncodedCharCount(size_t decodedByteCount)
{
    const size_t groups = decodedByteCount / 3 + (decodedByteCount % 3 == 0 ? 0 : 1);
    if (groups > std::numeric_limits<size_t>::max() / 4)
        throw std::overflow_error("VTK XML base64 encoded byte count overflow");
    return groups * 4;
}

class Base64PrefixDecoder
{
public:
    /// Decodes only enough inline base64 to inspect a VTK XML binary-block
    /// header. It leaves the stream positioned after the consumed prefix and
    /// counts encoded bytes so the caller can seek over the rest of the
    /// contiguous block.
    explicit Base64PrefixDecoder(StreamingXmlReader* reader) : _reader(reader)
    {
    }

    void DecodeUntil(size_t byteCount)
    {
        while (_decoded.size() < byteCount)
        {
            const int next = _reader->Get();
            if (next == EOF)
                throw cae::FileFormatError("VTK XML inline binary payload is truncated");
            const auto c = static_cast<char>(next);
            if (IsXmlWhitespace(c))
                throw cae::FileFormatError("VTK XML inline binary base64 payload contains embedded whitespace");

            ++_encodedChars;
            if (c == '=')
            {
                _seenPadding = true;
                continue;
            }
            if (_seenPadding)
                throw cae::FileFormatError("Malformed VTK XML base64 payload");

            const int decoded = Base64DecodeValue(static_cast<unsigned char>(c));
            if (decoded < 0)
                throw cae::FileFormatError("Malformed VTK XML base64 payload");

            _value = (_value << 6) | decoded;
            _bits += 6;
            if (_bits >= 0)
            {
                _decoded.push_back(static_cast<unsigned char>((_value >> _bits) & 0xff));
                _bits -= 8;
            }
        }
    }

    const std::vector<unsigned char>& Bytes() const
    {
        return _decoded;
    }

    size_t EncodedCharsConsumed() const
    {
        return _encodedChars;
    }

private:
    StreamingXmlReader* _reader = nullptr;
    std::vector<unsigned char> _decoded;
    size_t _encodedChars = 0;
    int _value = 0;
    int _bits = -8;
    bool _seenPadding = false;
};

struct InlineBinaryBlockBounds
{
    size_t storageByteCount = 0;
    uint64_t decodedPayloadByteCount = 0;
    size_t encodedCharCount = 0;
    size_t firstSegmentEncodedCharCount = 0;
    size_t secondSegmentEncodedCharCount = 0;
    size_t prefixEncodedChars = 0;
};

/// Decode the metadata prefix for an inline VTK XML binary block.
///
/// VTK packs inline binary arrays as base64 text around the same binary-block
/// layout used by appended data. This function decodes only the header and
/// compressed-size table needed to learn the encoded source range and decoded
/// payload size; it never decodes the scalar payload itself.
InlineBinaryBlockBounds ReadInlineBinaryBlockHeader(StreamingXmlReader* reader,
                                                    XmlHeaderType headerType,
                                                    ByteOrder byteOrder,
                                                    XmlCompressor compressor)
{
    const size_t wordSize = XmlHeaderWordSize(headerType);
    Base64PrefixDecoder decoder(reader);
    decoder.DecodeUntil(wordSize);
    const uint64_t firstWord = ReadXmlEndianUnsigned(decoder.Bytes().data(), wordSize, byteOrder);

    InlineBinaryBlockBounds bounds;
    if (compressor == XmlCompressor::None)
    {
        bounds.decodedPayloadByteCount = firstWord;
        bounds.storageByteCount = CheckedAddSize(wordSize, CheckedSize(firstWord, "VTK XML inline binary byte count"));
        bounds.encodedCharCount = Base64EncodedCharCount(bounds.storageByteCount);
        bounds.firstSegmentEncodedCharCount = bounds.encodedCharCount;
        bounds.prefixEncodedChars = decoder.EncodedCharsConsumed();
        return bounds;
    }

    decoder.DecodeUntil(wordSize * 3);
    const std::vector<unsigned char>& bytes = decoder.Bytes();
    const size_t numberOfBlocks = CheckedSize(firstWord, "VTK XML inline compressed block count exceeds size_t");
    const uint64_t blockSize = ReadXmlEndianUnsigned(bytes.data() + wordSize, wordSize, byteOrder);
    const uint64_t lastBlockSize = ReadXmlEndianUnsigned(bytes.data() + 2 * wordSize, wordSize, byteOrder);
    const size_t tableByteCount = CheckedMul(wordSize, CheckedAddSize(3, numberOfBlocks));
    decoder.DecodeUntil(tableByteCount);

    size_t compressedByteCount = 0;
    for (size_t block = 0; block < numberOfBlocks; ++block)
    {
        const size_t offset = wordSize * (3 + block);
        const size_t blockBytes = CheckedSize(ReadXmlEndianUnsigned(bytes.data() + offset, wordSize, byteOrder),
                                              "VTK XML inline compressed block byte count exceeds size_t");
        compressedByteCount = CheckedAddSize(compressedByteCount, blockBytes);
    }

    bounds.storageByteCount = CheckedAddSize(tableByteCount, compressedByteCount);
    bounds.decodedPayloadByteCount =
        numberOfBlocks == 0 ?
            0 :
            CheckedAddSize(CheckedMul(CheckedSize(blockSize, "VTK XML inline block size"), numberOfBlocks - 1),
                           CheckedSize(lastBlockSize, "VTK XML inline last block size"));
    // VTK finalizes the compression header and compressed block payload as
    // separate base64 streams, so padding may appear between the two encoded
    // segments even though the decoded bytes are one logical binary block.
    bounds.encodedCharCount =
        CheckedAddSize(Base64EncodedCharCount(tableByteCount), Base64EncodedCharCount(compressedByteCount));
    bounds.firstSegmentEncodedCharCount = Base64EncodedCharCount(tableByteCount);
    bounds.secondSegmentEncodedCharCount = Base64EncodedCharCount(compressedByteCount);
    bounds.prefixEncodedChars = decoder.EncodedCharsConsumed();
    if (bounds.prefixEncodedChars > bounds.encodedCharCount)
        throw cae::FileFormatError("VTK XML inline binary header exceeds encoded block size");
    return bounds;
}

/// Seek over the remaining bytes of a VTK inline base64 block.
///
/// VTK's base64 input stream is byte-position based and expects the encoded
/// block to be contiguous. We allow XML whitespace before and after the block,
/// but a seek landing anywhere except the expected XML terminator means the
/// payload contains embedded whitespace or the block length is inconsistent.
void SkipContiguousEncodedChars(StreamingXmlReader* reader,
                                size_t encodedChars,
                                std::string_view terminator,
                                uint64_t* payloadEnd)
{
    const uint64_t begin = reader->Position();
    const uint64_t end = CheckedOffset(begin, encodedChars);
    if (end > reader->FileSize())
        throw cae::FileFormatError("VTK XML inline binary payload is truncated while skipping");

    reader->Seek(end);
    *payloadEnd = reader->Position();
    reader->SkipWhitespace();
    if (reader->StartsWith(terminator))
        return;

    throw cae::FileFormatError(
        "VTK XML inline binary base64 payload is not a contiguous block followed by the expected terminator");
}

/// Record source offsets for an inline binary `DataArray`.
///
/// The DOM keeps only metadata and synthetic offset attributes. The actual
/// array remains in the source file and will be decoded later by the payload
/// reader using the recorded base64 byte range.
void RecordInlineBinaryPayload(pugi::xml_node node, StreamingXmlReader* reader, const XmlParseContext& ctx)
{
    reader->SkipWhitespace();
    const bool inCData = reader->Consume("<![CDATA[");
    const uint64_t payloadStart = reader->SkipWhitespace();
    const InlineBinaryBlockBounds bounds =
        ReadInlineBinaryBlockHeader(reader, ctx.headerType, ctx.byteOrder, ctx.compressor);
    const size_t remainingEncodedChars = bounds.encodedCharCount - bounds.prefixEncodedChars;

    uint64_t payloadEnd = reader->Position();
    const std::string_view terminator = inCData ? std::string_view("]]>") : std::string_view("<");
    SkipContiguousEncodedChars(reader, remainingEncodedChars, terminator, &payloadEnd);
    if (inCData)
    {
        if (!reader->Consume("]]>"))
            throw cae::FileFormatError("VTK XML inline binary CDATA section is unterminated");
        reader->SkipWhitespace();
    }

    node.append_attribute(PayloadStartOffsetAttr).set_value(std::to_string(payloadStart).c_str());
    node.append_attribute(PayloadEndOffsetAttr).set_value(std::to_string(payloadEnd).c_str());
    node.append_attribute(PayloadStorageByteCountAttr).set_value(std::to_string(payloadEnd - payloadStart).c_str());
    node.append_attribute(PayloadFirstSegmentByteCountAttr)
        .set_value(std::to_string(bounds.firstSegmentEncodedCharCount).c_str());
    node.append_attribute(PayloadSecondSegmentByteCountAttr)
        .set_value(std::to_string(bounds.secondSegmentEncodedCharCount).c_str());
    node.append_attribute(BinaryDecodedByteCountAttr).set_value(std::to_string(bounds.decodedPayloadByteCount).c_str());
}

struct InlineAsciiScanResult
{
    uint64_t payloadEnd = 0;
    size_t valueCount = 0;
};

InlineAsciiScanResult ScanInlineAsciiPayload(StreamingXmlReader* reader, uint64_t payloadStart, std::string_view terminator)
{
    InlineAsciiScanResult result;
    result.payloadEnd = payloadStart;
    bool inToken = false;

    while (true)
    {
        if (reader->StartsWith(terminator))
            break;

        const int next = reader->Get();
        if (next == EOF)
            throw cae::FileFormatError("VTK XML ASCII DataArray is unterminated");

        const auto c = static_cast<char>(next);
        if (IsXmlWhitespace(c))
        {
            inToken = false;
        }
        else
        {
            if (!inToken)
            {
                ++result.valueCount;
                inToken = true;
            }
            result.payloadEnd = reader->Position();
        }
    }
    return result;
}

/// Record source offsets for an inline ASCII `DataArray`.
///
/// ASCII payloads carry no byte count, so the tokenizer must scan to the end of
/// the payload. It counts scalar tokens while scanning, but keeps the DOM small
/// by not attaching the text to the pugi node.
///
/// A `DataArray` may carry nested child elements such as `<InformationKey>`
/// after its numeric data, so the scan stops at the first `<` -- the start of
/// the next element, whether a child or the closing `</DataArray>`. The main
/// tokenizer loop then parses those children as ordinary metadata nodes. (A
/// CDATA payload is raw text instead, so it scans to its `]]>` terminator.)
void RecordInlineAsciiPayload(pugi::xml_node node, StreamingXmlReader* reader)
{
    reader->SkipWhitespace();
    const bool inCData = reader->Consume("<![CDATA[");
    const uint64_t payloadStart = reader->SkipWhitespace();
    InlineAsciiScanResult scan = ScanInlineAsciiPayload(reader, payloadStart, inCData ? "]]>" : "<");
    if (inCData)
    {
        if (!reader->Consume("]]>"))
            throw cae::FileFormatError("VTK XML inline ASCII CDATA section is unterminated");
        reader->SkipWhitespace();
    }

    node.append_attribute(PayloadStartOffsetAttr).set_value(std::to_string(payloadStart).c_str());
    node.append_attribute(PayloadEndOffsetAttr).set_value(std::to_string(scan.payloadEnd).c_str());
    node.append_attribute(PayloadStorageByteCountAttr).set_value(std::to_string(scan.payloadEnd - payloadStart).c_str());
    node.append_attribute(AsciiValueCountAttr).set_value(std::to_string(scan.valueCount).c_str());
}

/// Locate the close tag for encoded appended payloads from the file tail.
///
/// Base64 appended data can be much larger than the metadata. The search walks
/// backward in fixed-size chunks with a small overlap so malformed files cannot
/// force the tokenizer to buffer the whole source.
uint64_t FindAppendedDataCloseFromTail(StreamingXmlReader* reader)
{
    constexpr size_t ChunkSize = 1u << 20;
    const size_t overlap = AppendedDataCloseTag.size() - 1;
    uint64_t searchEnd = reader->FileSize();
    std::string higherPrefix;

    while (searchEnd > 0)
    {
        const size_t chunkSize = CheckedSize(std::min<uint64_t>(ChunkSize, searchEnd), "VTK XML tail search chunk");
        const uint64_t offset = searchEnd - chunkSize;
        std::string text = reader->ReadRange(offset, chunkSize);
        text += higherPrefix;

        const size_t close = text.rfind(std::string(AppendedDataCloseTag));
        if (close != std::string::npos)
            return offset + close;

        if (offset == 0)
            break;

        higherPrefix = text.substr(0, std::min(overlap, text.size()));
        searchEnd = offset;
    }
    throw cae::FileFormatError("VTK XML appended data close tag was not found");
}

bool SkipXmlSpecialConstruct(StreamingXmlReader* reader, int next)
{
    if (next == '?')
    {
        reader->Get();
        reader->SkipUntil("?>");
        return true;
    }
    if (next != '!')
        return false;

    reader->Get();
    if (reader->Consume("--"))
        reader->SkipUntil("-->");
    else if (reader->Consume("[CDATA["))
        reader->SkipUntil("]]>");
    else
        reader->ReadTagBody();
    return true;
}

void CloseXmlElement(const std::string& tagBody,
                     uint64_t tagOffset,
                     const std::string& filePath,
                     std::vector<pugi::xml_node>* stack,
                     std::vector<std::string>* stackNames)
{
    if (const std::string closeName = ParseXmlCloseTagName(tagBody);
        stackNames->empty() || stackNames->back() != closeName)
        throw cae::FileFormatError("VTK XML element nesting is invalid near byte " + std::to_string(tagOffset) + ": " +
                                   filePath);
    stackNames->pop_back();
    stack->pop_back();
}

pugi::xml_node AppendXmlElement(pugi::xml_document* doc,
                                const ParsedXmlTag& tag,
                                const std::string& filePath,
                                std::vector<pugi::xml_node>& stack,
                                bool* rootSeen)
{
    if (stack.empty() && *rootSeen)
        throw cae::FileFormatError("VTK XML document has more than one root element: " + filePath);

    pugi::xml_node node =
        stack.empty() ? doc->append_child(tag.name.c_str()) : stack.back().append_child(tag.name.c_str());
    for (const ParsedXmlAttribute& attribute : tag.attributes)
        node.append_attribute(attribute.name.c_str()).set_value(attribute.value.c_str());
    if (stack.empty())
        *rootSeen = true;
    return node;
}

bool HandleXmlPayload(const ParsedXmlTag& tag,
                      pugi::xml_node node,
                      StreamingXmlReader* reader,
                      XmlParseContext* context,
                      XmlEnvelope* envelope)
{
    if (tag.name == "VTKFile")
    {
        context->byteOrder = ParseXmlByteOrder(XmlAttribute(node, "byte_order", "LittleEndian"));
        context->headerType = ParseXmlHeaderType(XmlAttribute(node, "header_type", "UInt32"));
        context->compressor = ParseXmlCompressor(XmlAttribute(node, "compressor"));
        return false;
    }
    if (tag.name == "DataArray" && !tag.selfClosing)
    {
        if (const std::string format = ToLower(XmlAttribute(node, "format", "ascii")); format == "binary")
            RecordInlineBinaryPayload(node, reader, *context);
        else if (format == "ascii")
            RecordInlineAsciiPayload(node, reader);
        return false;
    }
    if (tag.name != "AppendedData")
        return false;

    const std::string encoding = ToLower(XmlAttribute(node, "encoding", "base64"));
    reader->SkipWhitespace();
    if (reader->Peek() == '_')
        reader->Get();

    envelope->hasAppendedData = true;
    envelope->appendedEncodingRaw = encoding == "raw";
    envelope->appendedDataOffset = reader->Position();
    node.append_attribute(AppendedDataOffsetAttr).set_value(std::to_string(envelope->appendedDataOffset).c_str());
    if (envelope->appendedEncodingRaw)
        return true;

    const uint64_t closeOffset = FindAppendedDataCloseFromTail(reader);
    envelope->appendedEncodedByteCount = closeOffset - envelope->appendedDataOffset;
    node.append_attribute(AppendedEncodedByteCountAttr)
        .set_value(std::to_string(envelope->appendedEncodedByteCount).c_str());
    reader->Seek(closeOffset);
    return false;
}

/// Stream source XML into a metadata-only pugi DOM.
///
/// This is the bridge between the hand-written tokenizer and the rest of
/// XmlParser: source elements/attributes are appended to pugi as they are seen,
/// while heavy payload text is replaced with reserved `__cae_` attributes that
/// describe the byte ranges needed by DatasetSpec payload readers.
pugi::xml_document ReadXmlMetadataDom(const std::string& filePath, XmlEnvelope* envelope)
{
    StreamingXmlReader reader(filePath);
    pugi::xml_document doc;
    std::vector<pugi::xml_node> stack;
    std::vector<std::string> stackNames;
    bool rootSeen = false;
    bool stoppedAtRawAppended = false;

    XmlParseContext tokenizerContext;
    tokenizerContext.filePath = filePath;

    while (reader.SeekToChar('<'))
    {
        const uint64_t tagOffset = reader.Position();
        reader.Get();

        const int next = reader.Peek();
        if (next == EOF)
            break;
        if (SkipXmlSpecialConstruct(&reader, next))
            continue;

        const std::string tagBody = reader.ReadTagBody();
        if (next == '/')
        {
            CloseXmlElement(tagBody, tagOffset, filePath, &stack, &stackNames);
            continue;
        }

        ParsedXmlTag tag = ParseXmlStartTag(tagBody);
        pugi::xml_node node = AppendXmlElement(&doc, tag, filePath, stack, &rootSeen);
        stoppedAtRawAppended = HandleXmlPayload(tag, node, &reader, &tokenizerContext, envelope);
        if (stoppedAtRawAppended)
            break;

        if (!tag.selfClosing)
        {
            stack.push_back(node);
            stackNames.push_back(tag.name);
        }
    }

    if (!rootSeen)
        throw cae::FileFormatError("VTK XML document is empty: " + filePath);
    if (!stack.empty() && !stoppedAtRawAppended)
        throw cae::FileFormatError("VTK XML document has unclosed elements: " + filePath);
    return doc;
}

int64_t ParseInt64Text(const std::string& text, int64_t fallback)
{
    if (text.empty())
        return fallback;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size())
        throw cae::FileFormatError("Failed to parse VTK XML integer value: " + text);
    return static_cast<int64_t>(value);
}

std::array<int, 6> ParseXmlExtent(const std::string& text)
{
    std::array<int, 6> values = { 0, -1, 0, -1, 0, -1 };
    std::istringstream stream(text);
    for (int& value : values)
    {
        if (!(stream >> value))
            throw cae::FileFormatError("Failed to parse VTK XML extent: " + text);
    }
    return values;
}

std::array<double, 3> ParseXmlVec3d(const std::string& text, std::array<double, 3> fallback)
{
    if (text.empty())
        return fallback;

    std::array<double, 3> values{};
    std::istringstream stream(text);
    for (double& value : values)
    {
        if (!(stream >> value))
            throw cae::FileFormatError("Failed to parse VTK XML 3-vector: " + text);
    }
    return values;
}

size_t ExtentPointCount(const std::array<int, 3>& minExtent, const std::array<int, 3>& maxExtent)
{
    const auto nx = static_cast<size_t>(std::max(maxExtent[0] - minExtent[0] + 1, 0));
    const auto ny = static_cast<size_t>(std::max(maxExtent[1] - minExtent[1] + 1, 0));
    const auto nz = static_cast<size_t>(std::max(maxExtent[2] - minExtent[2] + 1, 0));
    return CheckedMul(CheckedMul(nx, ny), nz);
}

size_t ExtentCellCount(const std::array<int, 3>& minExtent, const std::array<int, 3>& maxExtent)
{
    const auto nx = static_cast<size_t>(std::max(maxExtent[0] - minExtent[0], 0));
    const auto ny = static_cast<size_t>(std::max(maxExtent[1] - minExtent[1], 0));
    const auto nz = static_cast<size_t>(std::max(maxExtent[2] - minExtent[2], 0));
    return CheckedMul(CheckedMul(nx, ny), nz);
}

size_t ExtentAxisPointCount(const DatasetSpec& dataset, int axis)
{
    return static_cast<size_t>(std::max(dataset.maxExtent[axis] - dataset.minExtent[axis] + 1, 0));
}

void ApplyXmlExtent(DatasetSpec* dataset, const std::string& text)
{
    const std::array<int, 6> extent = ParseXmlExtent(text);
    dataset->minExtent = { extent[0], extent[2], extent[4] };
    dataset->maxExtent = { extent[1], extent[3], extent[5] };
    dataset->pointCount = ExtentPointCount(dataset->minExtent, dataset->maxExtent);
    dataset->cellCount = ExtentCellCount(dataset->minExtent, dataset->maxExtent);
}

ByteOrder ParseXmlByteOrder(const std::string& value)
{
    const std::string lower = ToLower(value);
    if (lower.empty() || lower == "littleendian")
        return ByteOrder::LittleEndian;
    if (lower == "bigendian")
        return ByteOrder::BigEndian;
    throw cae::FileFormatError("Unsupported VTK XML byte_order: " + value);
}

XmlHeaderType ParseXmlHeaderType(const std::string& value)
{
    const std::string lower = ToLower(value);
    if (lower.empty() || lower == "uint32")
        return XmlHeaderType::UInt32;
    if (lower == "uint64")
        return XmlHeaderType::UInt64;
    throw cae::FileFormatError("Unsupported VTK XML header_type: " + value);
}

XmlCompressor ParseXmlCompressor(const std::string& value)
{
    if (value.empty())
        return XmlCompressor::None;
    if (value == "vtkZLibDataCompressor")
        return XmlCompressor::ZLib;
    if (value == "vtkLZ4DataCompressor")
        return XmlCompressor::Lz4;
    if (value == "vtkLZMADataCompressor")
        return XmlCompressor::Lzma;
    throw cae::FileFormatError("Unsupported VTK XML compressor: " + value);
}

size_t XmlHeaderWordSize(XmlHeaderType headerType)
{
    switch (headerType)
    {
    case XmlHeaderType::UInt32:
        return sizeof(uint32_t);
    case XmlHeaderType::UInt64:
        return sizeof(uint64_t);
    }
    throw cae::FileFormatError("Unsupported VTK XML header type");
}

uint64_t ReadXmlEndianUnsigned(const unsigned char* bytes, size_t byteCount, ByteOrder byteOrder)
{
    uint64_t value = 0;
    if (byteOrder == ByteOrder::BigEndian)
    {
        for (size_t i = 0; i < byteCount; ++i)
            value = (value << 8) | static_cast<uint64_t>(bytes[i]);
    }
    else
    {
        for (size_t i = 0; i < byteCount; ++i)
            value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    }
    return value;
}

std::vector<unsigned char> ReadFileBytes(const std::string& filePath, uint64_t offset, size_t byteCount)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open VTK XML file: " + filePath);
    SeekAbsolute(input, offset);

    std::vector<unsigned char> bytes(byteCount);
    if (byteCount == 0)
        return bytes;
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<size_t>(input.gcount()) != byteCount)
        throw cae::FileFormatError("Failed to read VTK XML metadata byte range: " + filePath);
    return bytes;
}

std::string ReadFileText(const std::string& filePath, uint64_t offset, size_t byteCount)
{
    std::vector<unsigned char> bytes = ReadFileBytes(filePath, offset, byteCount);
    if (bytes.empty())
        return {};
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

size_t SkipBase64Whitespace(const std::string& text, size_t cursor)
{
    while (cursor < text.size() && IsXmlWhitespace(text[cursor]))
        ++cursor;
    return cursor;
}

std::vector<unsigned char> DecodeBase64Quartet(const char quartet[4])
{
    std::array<int, 4> values = { 0, 0, 0, 0 };
    int padding = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (quartet[i] == '=')
        {
            values[i] = 0;
            ++padding;
            if (i < 2)
                throw cae::FileFormatError("Malformed VTK XML appended base64 padding");
            continue;
        }

        if (padding != 0)
            throw cae::FileFormatError("Malformed VTK XML appended base64 padding");
        values[i] = Base64DecodeValue(static_cast<unsigned char>(quartet[i]));
        if (values[i] < 0)
            throw cae::FileFormatError("Malformed VTK XML appended base64 payload");
    }

    std::vector<unsigned char> decoded;
    decoded.reserve(3);
    decoded.push_back(static_cast<unsigned char>((values[0] << 2) | (values[1] >> 4)));
    if (padding < 2)
        decoded.push_back(static_cast<unsigned char>(((values[1] & 0x0f) << 4) | (values[2] >> 2)));
    if (padding == 0)
        decoded.push_back(static_cast<unsigned char>(((values[2] & 0x03) << 6) | values[3]));
    return decoded;
}

std::vector<unsigned char> ReadBase64Quartet(const std::string& text, size_t* cursor)
{
    *cursor = SkipBase64Whitespace(text, *cursor);
    std::array<char, 4> quartet = {};
    for (int i = 0; i < 4; ++i)
    {
        if (*cursor >= text.size())
            throw cae::FileFormatError("VTK XML appended base64 payload is truncated");
        const char c = text[(*cursor)++];
        if (IsXmlWhitespace(c))
            throw cae::FileFormatError("VTK XML appended base64 payload contains embedded whitespace");
        quartet[i] = c;
    }
    return DecodeBase64Quartet(quartet.data());
}

std::vector<unsigned char> DecodeBase64Prefix(const std::string& text, size_t decodedByteCount)
{
    std::vector<unsigned char> decoded;
    decoded.reserve(decodedByteCount);
    size_t cursor = SkipBase64Whitespace(text, 0);
    while (decoded.size() < decodedByteCount)
    {
        std::vector<unsigned char> quartet = ReadBase64Quartet(text, &cursor);
        decoded.insert(decoded.end(), quartet.begin(), quartet.end());
    }
    decoded.resize(decodedByteCount);
    return decoded;
}

void ValidateAppendedBase64Range(const XmlParseContext& ctx, uint64_t encodedOffset, size_t encodedByteCount)
{
    if (encodedOffset > ctx.appendedEncodedByteCount || encodedByteCount > ctx.appendedEncodedByteCount - encodedOffset)
    {
        throw cae::FileFormatError("VTK XML appended base64 segment exceeds AppendedData payload");
    }
}

std::vector<unsigned char> DecodeAppendedBase64Prefix(const XmlParseContext& ctx,
                                                      uint64_t encodedOffset,
                                                      size_t decodedByteCount)
{
    if ((encodedOffset % 4) != 0)
        throw cae::FileFormatError("VTK XML appended base64 offset is not quartet-aligned");

    const size_t encodedByteCount = Base64EncodedCharCount(decodedByteCount);
    ValidateAppendedBase64Range(ctx, encodedOffset, encodedByteCount);
    const uint64_t absoluteOffset =
        CheckedOffset(ctx.appendedDataOffset, CheckedSize(encodedOffset, "VTK XML appended offset exceeds size_t"));
    return DecodeBase64Prefix(ReadFileText(ctx.filePath, absoluteOffset, encodedByteCount), decodedByteCount);
}

uint64_t ReadXmlBlockDecodedByteCount(const unsigned char* headerBytes,
                                      size_t headerByteCount,
                                      XmlHeaderType headerType,
                                      ByteOrder byteOrder,
                                      XmlCompressor compressor)
{
    const size_t wordSize = XmlHeaderWordSize(headerType);
    if (headerByteCount < wordSize)
        throw cae::FileFormatError("VTK XML binary block header is truncated");

    if (compressor == XmlCompressor::None)
        return ReadXmlEndianUnsigned(headerBytes, wordSize, byteOrder);

    if (headerByteCount < wordSize * 3)
        throw cae::FileFormatError("VTK XML compressed block header is truncated");

    const uint64_t numberOfBlocks = ReadXmlEndianUnsigned(headerBytes, wordSize, byteOrder);
    const uint64_t blockSize = ReadXmlEndianUnsigned(headerBytes + wordSize, wordSize, byteOrder);
    const uint64_t lastBlockSize = ReadXmlEndianUnsigned(headerBytes + 2 * wordSize, wordSize, byteOrder);
    if (numberOfBlocks == 0)
        return 0;
    if (numberOfBlocks - 1 > (std::numeric_limits<uint64_t>::max() - lastBlockSize) / blockSize)
        throw std::overflow_error("VTK XML compressed payload decoded byte count overflow");
    return (numberOfBlocks - 1) * blockSize + lastBlockSize;
}

struct XmlCompressedBlockSourceLayout
{
    size_t headerByteCount = 0;
    size_t compressedByteCount = 0;
};

XmlCompressedBlockSourceLayout ParseCompressedXmlBlockSourceLayout(const unsigned char* headerBytes,
                                                                   size_t headerByteCount,
                                                                   XmlHeaderType headerType,
                                                                   ByteOrder byteOrder)
{
    const size_t wordSize = XmlHeaderWordSize(headerType);
    if (headerByteCount < wordSize * 3)
        throw cae::FileFormatError("VTK XML compressed block header is truncated");

    const size_t numberOfBlocks =
        CheckedSize(ReadXmlEndianUnsigned(headerBytes, wordSize, byteOrder), "VTK XML compressed block count overflow");
    const size_t expectedHeaderByteCount = CheckedMul(wordSize, CheckedAddSize(3, numberOfBlocks));
    if (headerByteCount < expectedHeaderByteCount)
        throw cae::FileFormatError("VTK XML compressed block size table is truncated");

    XmlCompressedBlockSourceLayout layout;
    layout.headerByteCount = expectedHeaderByteCount;
    for (size_t block = 0; block < numberOfBlocks; ++block)
    {
        const size_t offset = wordSize * (3 + block);
        layout.compressedByteCount = CheckedAddSize(
            layout.compressedByteCount, CheckedSize(ReadXmlEndianUnsigned(headerBytes + offset, wordSize, byteOrder),
                                                    "VTK XML compressed block byte count exceeds size_t"));
    }
    return layout;
}

XmlCompressedBlockSourceLayout ReadRawCompressedXmlBlockSourceLayout(const XmlParseContext& ctx, uint64_t blockOffset)
{
    const size_t wordSize = XmlHeaderWordSize(ctx.headerType);
    const uint64_t absoluteOffset =
        CheckedOffset(ctx.appendedDataOffset, CheckedSize(blockOffset, "VTK XML appended offset exceeds size_t"));

    const std::vector<unsigned char> prefix = ReadFileBytes(ctx.filePath, absoluteOffset, wordSize * 3);
    const size_t numberOfBlocks = CheckedSize(
        ReadXmlEndianUnsigned(prefix.data(), wordSize, ctx.byteOrder), "VTK XML compressed block count overflow");
    const size_t headerByteCount = CheckedMul(wordSize, CheckedAddSize(3, numberOfBlocks));
    const std::vector<unsigned char> header = ReadFileBytes(ctx.filePath, absoluteOffset, headerByteCount);
    return ParseCompressedXmlBlockSourceLayout(header.data(), header.size(), ctx.headerType, ctx.byteOrder);
}

XmlCompressedBlockSourceLayout ReadBase64CompressedXmlBlockSourceLayout(const XmlParseContext& ctx, uint64_t blockOffset)
{
    const size_t wordSize = XmlHeaderWordSize(ctx.headerType);
    const std::vector<unsigned char> prefix = DecodeAppendedBase64Prefix(ctx, blockOffset, wordSize * 3);
    const size_t numberOfBlocks = CheckedSize(
        ReadXmlEndianUnsigned(prefix.data(), wordSize, ctx.byteOrder), "VTK XML compressed block count overflow");
    const size_t headerByteCount = CheckedMul(wordSize, CheckedAddSize(3, numberOfBlocks));
    const std::vector<unsigned char> header = DecodeAppendedBase64Prefix(ctx, blockOffset, headerByteCount);
    return ParseCompressedXmlBlockSourceLayout(header.data(), header.size(), ctx.headerType, ctx.byteOrder);
}

PayloadSourceSpec MakeInlineXmlBinaryPayloadSource(const XmlParseContext& ctx, const pugi::xml_node& node)
{
    const uint64_t startOffset = XmlUnsignedAttribute(node, PayloadStartOffsetAttr);
    const size_t firstSegmentByteCount = CheckedSize(XmlUnsignedAttribute(node, PayloadFirstSegmentByteCountAttr),
                                                     "VTK XML inline first segment byte count exceeds size_t");
    const size_t secondSegmentByteCount = CheckedSize(XmlUnsignedAttribute(node, PayloadSecondSegmentByteCountAttr),
                                                      "VTK XML inline second segment byte count exceeds size_t");

    PayloadSourceSpec source;
    source.storageKind = StorageKind::XmlBase64Binary;
    source.segments.push_back({ startOffset, firstSegmentByteCount });
    if (ctx.compressor != XmlCompressor::None)
        source.segments.push_back({ CheckedOffset(startOffset, firstSegmentByteCount), secondSegmentByteCount });
    return source;
}

PayloadSourceSpec MakeRawAppendedXmlBinaryPayloadSource(const XmlParseContext& ctx,
                                                        uint64_t blockOffset,
                                                        const ScalarPayloadRequest& request)
{
    const uint64_t absoluteOffset =
        CheckedOffset(ctx.appendedDataOffset, CheckedSize(blockOffset, "VTK XML appended offset exceeds size_t"));

    PayloadSourceSpec source;
    source.storageKind = StorageKind::XmlBinary;
    if (ctx.compressor == XmlCompressor::None)
    {
        const size_t segmentByteCount = CheckedAddSize(XmlHeaderWordSize(ctx.headerType), DecodedByteCount(request));
        source.segments.push_back({ absoluteOffset, segmentByteCount });
        return source;
    }

    const XmlCompressedBlockSourceLayout layout = ReadRawCompressedXmlBlockSourceLayout(ctx, blockOffset);
    source.segments.push_back({ absoluteOffset, layout.headerByteCount });
    source.segments.push_back({ CheckedOffset(absoluteOffset, layout.headerByteCount), layout.compressedByteCount });
    return source;
}

PayloadSourceSpec MakeBase64AppendedXmlBinaryPayloadSource(const XmlParseContext& ctx,
                                                           uint64_t blockOffset,
                                                           const ScalarPayloadRequest& request)
{
    if ((blockOffset % 4) != 0)
        throw cae::FileFormatError("VTK XML appended base64 offset is not quartet-aligned");

    const uint64_t absoluteOffset =
        CheckedOffset(ctx.appendedDataOffset, CheckedSize(blockOffset, "VTK XML appended offset exceeds size_t"));

    PayloadSourceSpec source;
    source.storageKind = StorageKind::XmlBase64Binary;
    if (ctx.compressor == XmlCompressor::None)
    {
        const size_t decodedByteCount = CheckedAddSize(XmlHeaderWordSize(ctx.headerType), DecodedByteCount(request));
        const size_t encodedByteCount = Base64EncodedCharCount(decodedByteCount);
        ValidateAppendedBase64Range(ctx, blockOffset, encodedByteCount);
        source.segments.push_back({ absoluteOffset, encodedByteCount });
        return source;
    }

    const XmlCompressedBlockSourceLayout layout = ReadBase64CompressedXmlBlockSourceLayout(ctx, blockOffset);
    const size_t encodedHeaderByteCount = Base64EncodedCharCount(layout.headerByteCount);
    const uint64_t compressedOffset = CheckedOffset(blockOffset, encodedHeaderByteCount);
    const size_t encodedCompressedByteCount = Base64EncodedCharCount(layout.compressedByteCount);
    ValidateAppendedBase64Range(ctx, blockOffset, encodedHeaderByteCount);
    ValidateAppendedBase64Range(ctx, compressedOffset, encodedCompressedByteCount);
    source.segments.push_back({ absoluteOffset, encodedHeaderByteCount });
    source.segments.push_back({ CheckedOffset(absoluteOffset, encodedHeaderByteCount), encodedCompressedByteCount });
    return source;
}

int Base64DecodeValue(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

uint64_t InferRawXmlBlockDecodedByteCount(const XmlParseContext& ctx, uint64_t blockOffset)
{
    const size_t wordSize = XmlHeaderWordSize(ctx.headerType);
    const size_t headerBytes = ctx.compressor == XmlCompressor::None ? wordSize : wordSize * 3;
    const std::vector<unsigned char> bytes = ReadFileBytes(
        ctx.filePath,
        CheckedOffset(ctx.appendedDataOffset, CheckedSize(blockOffset, "VTK XML appended offset exceeds size_t")),
        headerBytes);
    return ReadXmlBlockDecodedByteCount(bytes.data(), bytes.size(), ctx.headerType, ctx.byteOrder, ctx.compressor);
}

InlineTextRange LocateInlineTextRange(const XmlParseContext& ctx, const pugi::xml_node& node)
{
    if (!node)
        throw cae::FileFormatError("Failed to locate inline VTK XML DataArray text in source file: " + ctx.filePath);

    const uint64_t offset = XmlUnsignedAttribute(node, PayloadStartOffsetAttr);
    const size_t byteCount = CheckedSize(
        XmlUnsignedAttribute(node, PayloadStorageByteCountAttr), "VTK XML inline payload byte count exceeds size_t");
    return { offset, byteCount };
}

pugi::xml_node FindFirstDataArray(const pugi::xml_node& node)
{
    return node.child("DataArray");
}

std::string XmlDebugContext(const XmlParseContext& ctx, std::string_view sourceName, std::string_view format)
{
    std::string result;
    result.reserve(ctx.filePath.size() + sourceName.size() + format.size() + 24);
    result.append(ctx.filePath).append(" DataArray='").append(sourceName).append("' format='").append(format).append("'");
    return result;
}

std::optional<size_t> InferXmlTupleCount(const XmlParseContext& ctx,
                                         const pugi::xml_node& node,
                                         ScalarType scalarType,
                                         int componentCount,
                                         std::string_view format,
                                         std::optional<size_t> contextTupleCount)
{
    const int64_t explicitTuples = ParseInt64Text(XmlAttribute(node, "NumberOfTuples"), -1);
    if (explicitTuples >= 0)
        return static_cast<size_t>(explicitTuples);
    if (contextTupleCount.has_value())
        return *contextTupleCount;

    if (format == "ascii")
    {
        const size_t valueCount =
            CheckedSize(XmlUnsignedAttribute(node, AsciiValueCountAttr), "VTK XML ASCII value count exceeds size_t");
        if (componentCount <= 0 || (valueCount % static_cast<size_t>(componentCount)) != 0)
            throw cae::FileFormatError("VTK XML ASCII DataArray value count is not divisible by components");
        return valueCount / static_cast<size_t>(componentCount);
    }

    uint64_t payloadByteCount = 0;
    if (format == "binary")
    {
        payloadByteCount = XmlUnsignedAttribute(node, BinaryDecodedByteCountAttr);
    }
    else if (format == "appended" && ctx.appendedEncodingRaw)
    {
        const auto offset = static_cast<uint64_t>(ParseInt64Text(XmlAttribute(node, "offset"), 0));
        payloadByteCount = InferRawXmlBlockDecodedByteCount(ctx, offset);
    }
    else if (format == "appended" && ctx.hasAppendedData)
    {
        const auto offset = static_cast<uint64_t>(ParseInt64Text(XmlAttribute(node, "offset"), 0));
        const size_t headerBytes = ctx.compressor == XmlCompressor::None ? XmlHeaderWordSize(ctx.headerType) :
                                                                           XmlHeaderWordSize(ctx.headerType) * 3;
        const std::vector<unsigned char> bytes = DecodeAppendedBase64Prefix(ctx, offset, headerBytes);
        payloadByteCount =
            ReadXmlBlockDecodedByteCount(bytes.data(), bytes.size(), ctx.headerType, ctx.byteOrder, ctx.compressor);
    }
    else
    {
        return std::nullopt;
    }

    const size_t tupleByteCount = CheckedMul(ScalarByteSize(scalarType), static_cast<size_t>(componentCount));
    if (tupleByteCount == 0 || (payloadByteCount % tupleByteCount) != 0)
        throw cae::FileFormatError("VTK XML binary DataArray byte count is not divisible by tuple size");
    return payloadByteCount / tupleByteCount;
}

void AddXmlArray(DatasetSpec* dataset,
                 ArrayNameRegistry* names,
                 XmlParseContext* ctx,
                 const pugi::xml_node& node,
                 const std::string& fallbackName,
                 Association association,
                 ArrayRole role,
                 std::optional<size_t> contextTupleCount,
                 int defaultComponentCount,
                 ArrayEvaluationKind forcedEvaluationKind)
{
    if (!node)
    {
        AddSkippedArray(dataset, fallbackName, association, role, {}, contextTupleCount.value_or(0),
                        defaultComponentCount, "missing VTK XML DataArray",
                        XmlDebugContext(*ctx, fallbackName, "missing"));
        return;
    }

    const std::string sourceName = XmlAttribute(node, "Name", fallbackName);
    const std::string sourceScalarToken = XmlAttribute(node, "type", "Float32");
    const std::string format = ToLower(XmlAttribute(node, "format", "ascii"));
    const auto componentCount =
        static_cast<int>(ParseInt64Text(XmlAttribute(node, "NumberOfComponents"), defaultComponentCount));
    const std::string debug = XmlDebugContext(*ctx, sourceName, format);

    if (componentCount <= 0)
    {
        AddSkippedArray(dataset, sourceName, association, role, sourceScalarToken, contextTupleCount.value_or(0),
                        componentCount, "invalid non-positive component count", debug);
        return;
    }

    ScalarTypeNormalization normalized = NormalizeXmlScalarTypeToken(sourceScalarToken);
    if (!normalized.supported)
    {
        AddSkippedArray(dataset, sourceName, association, role, sourceScalarToken, contextTupleCount.value_or(0),
                        componentCount, normalized.reason, debug);
        return;
    }

    std::optional<size_t> sourceTupleCount =
        InferXmlTupleCount(*ctx, node, normalized.scalarType, componentCount, format, contextTupleCount);
    if (!sourceTupleCount.has_value())
    {
        AddSkippedArray(dataset, sourceName, association, role, sourceScalarToken, 0, componentCount,
                        "cannot infer VTK XML DataArray tuple count without reading heavy payload", debug);
        return;
    }

    if (!SupportsArrayShape(normalized.scalarType, componentCount))
    {
        AddSkippedArray(dataset, sourceName, association, role, sourceScalarToken, *sourceTupleCount, componentCount,
                        "unsupported component count or non-floating vector array", debug);
        return;
    }

    ScalarPayloadSpec payload;
    payload.request.scalarType = normalized.scalarType;
    payload.request.valueCount = CheckedMul(*sourceTupleCount, static_cast<size_t>(componentCount));

    if (format == "ascii")
    {
        const InlineTextRange text = LocateInlineTextRange(*ctx, node);
        payload.source.storageKind = StorageKind::Ascii;
        payload.source.segments.push_back({ text.offset, text.byteCount });
    }
    else if (format == "binary")
    {
        payload.source = MakeInlineXmlBinaryPayloadSource(*ctx, node);
    }
    else if (format == "appended")
    {
        if (!ctx->hasAppendedData)
            throw cae::FileFormatError("VTK XML DataArray requests appended data, but AppendedData is missing");
        const auto blockOffset = static_cast<uint64_t>(ParseInt64Text(XmlAttribute(node, "offset"), 0));
        payload.source = ctx->appendedEncodingRaw ?
                             MakeRawAppendedXmlBinaryPayloadSource(*ctx, blockOffset, payload.request) :
                             MakeBase64AppendedXmlBinaryPayloadSource(*ctx, blockOffset, payload.request);
    }
    else
    {
        AddSkippedArray(dataset, sourceName, association, role, sourceScalarToken, *sourceTupleCount, componentCount,
                        "unsupported VTK XML DataArray format: " + format, debug);
        return;
    }

    ArraySpec array;
    array.sourceName = sourceName;
    array.arrayName =
        role == ArrayRole::Generic ? names->MakeUniqueName(sourceName, fallbackName) : PXR_NS::TfToken(fallbackName);
    array.scalarType = normalized.scalarType;
    array.tupleCount = forcedEvaluationKind == ArrayEvaluationKind::PrependZero ? CheckedAddSize(*sourceTupleCount, 1) :
                                                                                  *sourceTupleCount;
    array.componentCount = componentCount;
    array.association = association;
    array.role = role;
    array.evaluationKind = forcedEvaluationKind == ArrayEvaluationKind::DirectScalarPayload ?
                               EvaluationKindFor(normalized.scalarType, componentCount) :
                               forcedEvaluationKind;
    array.payload = std::move(payload);
    array.sourceScalarToken = sourceScalarToken;
    array.debugContext = debug;
    dataset->arrays.push_back(std::move(array));
}

void AddXmlDirectArray(DatasetSpec* dataset,
                       ArrayNameRegistry* names,
                       XmlParseContext* ctx,
                       const pugi::xml_node& node,
                       const std::string& fallbackName,
                       Association association,
                       ArrayRole role,
                       std::optional<size_t> contextTupleCount,
                       int defaultComponentCount)
{
    AddXmlArray(dataset, names, ctx, node, fallbackName, association, role, contextTupleCount, defaultComponentCount,
                ArrayEvaluationKind::DirectScalarPayload);
}

void AddXmlOffsetsArray(DatasetSpec* dataset,
                        ArrayNameRegistry* names,
                        XmlParseContext* ctx,
                        const pugi::xml_node& node,
                        const std::string& fallbackName,
                        std::optional<size_t> cellCount)
{
    AddXmlArray(dataset, names, ctx, node, fallbackName, Association::None, ArrayRole::Offsets, cellCount, 1,
                ArrayEvaluationKind::PrependZero);
}

void ParseXmlFieldData(DatasetSpec* dataset,
                       ArrayNameRegistry* names,
                       XmlParseContext* ctx,
                       const pugi::xml_node& parent,
                       Association association,
                       std::optional<size_t> tupleCount)
{
    if (!parent)
        return;

    const char* groupName = "fieldData";
    if (association == Association::Point)
        groupName = "pointData";
    else if (association == Association::Cell)
        groupName = "cellData";

    for (pugi::xml_node child : parent.children("DataArray"))
    {
        AddXmlDirectArray(dataset, names, ctx, child, groupName, association, ArrayRole::Generic, tupleCount, 1);
    }
}

pugi::xml_node FindDataArrayByName(const pugi::xml_node& parent, const char* name)
{
    if (!parent)
        return {};

    for (pugi::xml_node child : parent.children("DataArray"))
    {
        if (ToLower(XmlAttribute(child, "Name")) == name)
            return child;
    }
    return {};
}

void ParseXmlCellArrayNode(DatasetSpec* dataset,
                           ArrayNameRegistry* names,
                           XmlParseContext* ctx,
                           const pugi::xml_node& parent,
                           const std::string& offsetsName,
                           const std::string& connectivityName,
                           std::optional<size_t> cellCount)
{
    if (!parent)
        return;
    if (cellCount && *cellCount == 0 && !parent.child("DataArray"))
        return;

    AddXmlOffsetsArray(dataset, names, ctx, FindDataArrayByName(parent, "offsets"), offsetsName, cellCount);
    AddXmlDirectArray(dataset, names, ctx, FindDataArrayByName(parent, "connectivity"), connectivityName,
                      Association::None, ArrayRole::Connectivity, std::nullopt, 1);
}

DatasetKind ParseXmlDatasetKind(const std::string& type)
{
    if (type == "ImageData")
        return DatasetKind::ImageData;
    if (type == "StructuredGrid")
        return DatasetKind::StructuredGrid;
    if (type == "RectilinearGrid")
        return DatasetKind::RectilinearGrid;
    if (type == "PolyData")
        return DatasetKind::PolyData;
    if (type == "UnstructuredGrid")
        return DatasetKind::UnstructuredGrid;
    throw cae::FileFormatError("Unsupported VTK XML dataset type: " + type);
}

} // namespace

bool XmlParser::CanParse(const std::string& filePath) const
{
    std::string prefix = ReadFilePrefix(filePath);
    prefix.erase(prefix.begin(), std::find_if(prefix.begin(), prefix.end(), [](unsigned char c)
                                              { return c != 0xefu && c != 0xbbu && c != 0xbfu && !std::isspace(c); }));
    return StartsWith(prefix, "<?xml") ? Contains(prefix, "<VTKFile") : StartsWith(prefix, "<VTKFile");
}

DatasetSpec XmlParser::Parse(const std::string& filePath, const ReadOptions& /*options*/) const
{
    XmlEnvelope envelope;
    pugi::xml_document xmlDoc = ReadXmlMetadataDom(filePath, &envelope);
    const pugi::xml_node vtkFile = xmlDoc.child("VTKFile");
    if (!vtkFile)
        throw cae::FileFormatError("VTK XML file is missing VTKFile root: " + filePath);

    XmlParseContext ctx;
    ctx.filePath = filePath;
    ctx.byteOrder = ParseXmlByteOrder(XmlAttribute(vtkFile, "byte_order", "LittleEndian"));
    ctx.headerType = ParseXmlHeaderType(XmlAttribute(vtkFile, "header_type", "UInt32"));
    ctx.compressor = ParseXmlCompressor(XmlAttribute(vtkFile, "compressor"));
    ctx.hasAppendedData = envelope.hasAppendedData;
    ctx.appendedEncodingRaw = envelope.appendedEncodingRaw;
    ctx.appendedDataOffset = envelope.appendedDataOffset;
    ctx.appendedEncodedByteCount = envelope.appendedEncodedByteCount;

    DatasetSpec dataset;
    dataset.file.filePath = filePath;
    dataset.file.byteOrder = ctx.byteOrder;
    dataset.file.xmlHeaderType = ctx.headerType;
    dataset.file.xmlCompressor = ctx.compressor;
    dataset.file.debugContext = filePath;
    dataset.kind = ParseXmlDatasetKind(XmlAttribute(vtkFile, "type"));
    ArrayNameRegistry names;

    pugi::xml_node datasetNode;
    switch (dataset.kind)
    {
    case DatasetKind::StructuredPoints:
    case DatasetKind::ImageData:
        datasetNode = vtkFile.child("ImageData");
        break;
    case DatasetKind::StructuredGrid:
        datasetNode = vtkFile.child("StructuredGrid");
        break;
    case DatasetKind::RectilinearGrid:
        datasetNode = vtkFile.child("RectilinearGrid");
        break;
    case DatasetKind::PolyData:
        datasetNode = vtkFile.child("PolyData");
        break;
    case DatasetKind::UnstructuredGrid:
        datasetNode = vtkFile.child("UnstructuredGrid");
        break;
    }
    if (!datasetNode)
        throw cae::FileFormatError("VTK XML dataset element is missing: " + filePath);

    if (dataset.kind == DatasetKind::ImageData || dataset.kind == DatasetKind::StructuredGrid ||
        dataset.kind == DatasetKind::RectilinearGrid)
    {
        const std::string wholeExtent = XmlAttribute(datasetNode, "WholeExtent");
        if (!wholeExtent.empty())
            ApplyXmlExtent(&dataset, wholeExtent);
    }
    if (dataset.kind == DatasetKind::ImageData)
    {
        dataset.origin = ParseXmlVec3d(XmlAttribute(datasetNode, "Origin"), dataset.origin);
        dataset.spacing = ParseXmlVec3d(XmlAttribute(datasetNode, "Spacing"), dataset.spacing);
    }

    if (pugi::xml_node fieldData = datasetNode.child("FieldData"))
        ParseXmlFieldData(&dataset, &names, &ctx, fieldData, Association::None, std::nullopt);

    pugi::xml_node piece = datasetNode.child("Piece");
    if (!piece)
        throw cae::FileFormatError("VTK XML file is missing Piece element: " + filePath);
    if (piece.next_sibling("Piece"))
        throw cae::FileFormatError("Serial VTK XML parser expects exactly one Piece element: " + filePath);

    if (dataset.kind == DatasetKind::ImageData || dataset.kind == DatasetKind::StructuredGrid ||
        dataset.kind == DatasetKind::RectilinearGrid)
    {
        const std::string pieceExtent = XmlAttribute(piece, "Extent");
        if (!pieceExtent.empty())
            ApplyXmlExtent(&dataset, pieceExtent);
    }
    else if (dataset.kind == DatasetKind::PolyData)
    {
        dataset.pointCount = static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfPoints"), 0));
        dataset.cellCount = static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfVerts"), 0)) +
                            static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfLines"), 0)) +
                            static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfStrips"), 0)) +
                            static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfPolys"), 0));
    }
    else if (dataset.kind == DatasetKind::UnstructuredGrid)
    {
        dataset.pointCount = static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfPoints"), 0));
        dataset.cellCount = static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfCells"), 0));
    }

    if (pugi::xml_node pointData = piece.child("PointData"))
        ParseXmlFieldData(&dataset, &names, &ctx, pointData, Association::Point, dataset.pointCount);
    if (pugi::xml_node cellData = piece.child("CellData"))
        ParseXmlFieldData(&dataset, &names, &ctx, cellData, Association::Cell, dataset.cellCount);

    switch (dataset.kind)
    {
    case DatasetKind::StructuredPoints:
    case DatasetKind::ImageData:
        break;
    case DatasetKind::StructuredGrid:
        AddXmlDirectArray(&dataset, &names, &ctx, FindFirstDataArray(piece.child("Points")), "points",
                          Association::None, ArrayRole::Points, dataset.pointCount, 3);
        break;
    case DatasetKind::RectilinearGrid:
    {
        pugi::xml_node coords = piece.child("Coordinates");
        if (!coords)
            throw cae::FileFormatError("VTK XML RectilinearGrid is missing Coordinates: " + filePath);
        const pugi::xml_node xCoords = coords.child("DataArray");
        const pugi::xml_node yCoords = xCoords.next_sibling("DataArray");
        const pugi::xml_node zCoords = yCoords.next_sibling("DataArray");
        AddXmlDirectArray(&dataset, &names, &ctx, xCoords, "xCoordinates", Association::None, ArrayRole::XCoordinates,
                          ExtentAxisPointCount(dataset, 0), 1);
        AddXmlDirectArray(&dataset, &names, &ctx, yCoords, "yCoordinates", Association::None, ArrayRole::YCoordinates,
                          ExtentAxisPointCount(dataset, 1), 1);
        AddXmlDirectArray(&dataset, &names, &ctx, zCoords, "zCoordinates", Association::None, ArrayRole::ZCoordinates,
                          ExtentAxisPointCount(dataset, 2), 1);
        break;
    }
    case DatasetKind::PolyData:
    {
        AddXmlDirectArray(&dataset, &names, &ctx, FindFirstDataArray(piece.child("Points")), "points",
                          Association::None, ArrayRole::Points, dataset.pointCount, 3);
        ParseXmlCellArrayNode(&dataset, &names, &ctx, piece.child("Verts"), "vertsConnectivityOffsets",
                              "vertsConnectivityArray",
                              static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfVerts"), 0)));
        ParseXmlCellArrayNode(&dataset, &names, &ctx, piece.child("Lines"), "linesConnectivityOffsets",
                              "linesConnectivityArray",
                              static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfLines"), 0)));
        ParseXmlCellArrayNode(&dataset, &names, &ctx, piece.child("Polys"), "polysConnectivityOffsets",
                              "polysConnectivityArray",
                              static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfPolys"), 0)));
        ParseXmlCellArrayNode(&dataset, &names, &ctx, piece.child("Strips"), "stripsConnectivityOffsets",
                              "stripsConnectivityArray",
                              static_cast<size_t>(ParseInt64Text(XmlAttribute(piece, "NumberOfStrips"), 0)));
        break;
    }
    case DatasetKind::UnstructuredGrid:
    {
        AddXmlDirectArray(&dataset, &names, &ctx, FindFirstDataArray(piece.child("Points")), "points",
                          Association::None, ArrayRole::Points, dataset.pointCount, 3);
        pugi::xml_node cells = piece.child("Cells");
        ParseXmlCellArrayNode(
            &dataset, &names, &ctx, cells, "connectivityOffsets", "connectivityArray", dataset.cellCount);
        AddXmlDirectArray(&dataset, &names, &ctx, FindDataArrayByName(cells, "types"), "cellTypes", Association::None,
                          ArrayRole::CellTypes, dataset.cellCount, 1);
        if (pugi::xml_node faceOffsets = FindDataArrayByName(cells, "face_offsets"))
        {
            AddXmlOffsetsArray(&dataset, &names, &ctx, faceOffsets, "polyhedronFacesOffsets", std::nullopt);
        }
        if (pugi::xml_node faceConnectivity = FindDataArrayByName(cells, "face_connectivity"))
        {
            AddXmlDirectArray(&dataset, &names, &ctx, faceConnectivity, "polyhedronFacesConnectivityArray",
                              Association::None, ArrayRole::Connectivity, std::nullopt, 1);
        }
        if (pugi::xml_node polyhedronOffsets = FindDataArrayByName(cells, "polyhedron_offsets"))
        {
            AddXmlOffsetsArray(&dataset, &names, &ctx, polyhedronOffsets, "polyhedronFaceLocationsOffsets", std::nullopt);
        }
        if (pugi::xml_node polyhedronToFaces = FindDataArrayByName(cells, "polyhedron_to_faces"))
        {
            AddXmlDirectArray(&dataset, &names, &ctx, polyhedronToFaces, "polyhedronFaceLocationsConnectivityArray",
                              Association::None, ArrayRole::Connectivity, std::nullopt, 1);
        }
        if (pugi::xml_node packedFaces = FindDataArrayByName(cells, "faces"))
        {
            AddXmlDirectArray(&dataset, &names, &ctx, packedFaces, "polyhedronPackedFaces", Association::None,
                              ArrayRole::Connectivity, std::nullopt, 1);
        }
        if (pugi::xml_node packedFaceOffsets = FindDataArrayByName(cells, "faceoffsets"))
        {
            AddXmlDirectArray(&dataset, &names, &ctx, packedFaceOffsets, "polyhedronPackedFaceOffsets",
                              Association::None, ArrayRole::Offsets, dataset.cellCount, 1);
        }
        break;
    }
    }

    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] parsed XML summary file='%s' arrays=%zu skipped=%zu points=%zu cells=%zu appended=%d raw=%d\n",
             filePath.c_str(), dataset.arrays.size(), dataset.skippedArrays.size(), dataset.pointCount,
             dataset.cellCount, ctx.hasAppendedData ? 1 : 0, ctx.appendedEncodingRaw ? 1 : 0);
    return dataset;
}

} // namespace cae::vtk
