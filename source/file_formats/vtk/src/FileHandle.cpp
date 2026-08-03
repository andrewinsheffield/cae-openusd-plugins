// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "FileHandle.h"

#include "DisablePXRWarnings.h"
#include "FileFormatError.h"
#include "UninitializedVtArray.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/work/loops.h>
#include <pxr/base/work/threadLimits.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cae::vtk
{

namespace
{

constexpr size_t ParallelReadThreshold = 1u << 20;

// Express base64 decode batching in quartets so every batch naturally maps to
// whole decoded triples. Partial decoded triples are handled only by the
// explicit head/tail scratch paths in Base64PayloadSegmentReader.
constexpr size_t Base64QuartetsPerBatch = static_cast<size_t>(2) * 1024 * 1024;
constexpr size_t Base64DecodedBytesPerBatch = Base64QuartetsPerBatch * 3;

size_t CheckedSize(uint64_t value, const char* context)
{
    if (value > std::numeric_limits<size_t>::max())
        throw std::overflow_error(context);
    return value;
}

void CheckAdd(uint64_t a, uint64_t b)
{
    if (b > std::numeric_limits<uint64_t>::max() - a)
        throw std::overflow_error("VTK file byte offset overflow");
}

uint64_t CheckedOffset(uint64_t offset, size_t delta)
{
    const auto delta64 = uint64_t{ delta };
    if (CheckedSize(delta64, "VTK file byte offset overflow") != delta)
        throw std::overflow_error("VTK file byte offset overflow");
    CheckAdd(offset, delta64);
    return offset + delta64;
}

std::streamoff ToStreamOffset(uint64_t offset)
{
    if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw std::overflow_error("VTK file offset is too large for std::streamoff");
    return static_cast<std::streamoff>(offset);
}

void CheckTarget(unsigned char* target, size_t byteCount)
{
    if (byteCount != 0 && !target)
        throw cae::FileFormatError("VTK segment read target is null");
}

void CheckDecodedRange(size_t decodedSize, size_t skipByteCount, size_t byteCount)
{
    if (skipByteCount > decodedSize || byteCount > decodedSize - skipByteCount)
        throw cae::FileFormatError("VTK payload segment decoded byte range is out of bounds");
}

void ReadFileRangeInto(const std::string& filePath, uint64_t offset, unsigned char* dst, size_t byteCount)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open VTK file: " + filePath);

    input.seekg(ToStreamOffset(offset), std::ios::beg);
    if (!input)
        throw cae::FileFormatError("Failed to seek VTK file: " + filePath);

    input.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(byteCount));
    if (static_cast<size_t>(input.gcount()) != byteCount)
        throw cae::FileFormatError("Failed to read requested VTK file byte range: " + filePath);
}

size_t TaskCountForBytes(size_t byteCount, const ReadOptions& options)
{
    if (byteCount < ParallelReadThreshold || options.ioThreads <= 1)
        return 1;

    const auto requested = static_cast<size_t>(std::max(1, options.ioThreads));
    const auto workLimit = static_cast<size_t>(std::max(1u, PXR_NS::WorkGetConcurrencyLimit()));
    return std::max<size_t>(1, std::min({ requested, workLimit, byteCount }));
}

void ReadFileRangeIntoParallel(
    const std::string& filePath, uint64_t offset, unsigned char* dst, size_t byteCount, const ReadOptions& options)
{
    if (byteCount == 0)
        return;
    CheckTarget(dst, byteCount);

    const size_t taskCount = TaskCountForBytes(byteCount, options);
    if (taskCount == 1)
    {
        ReadFileRangeInto(filePath, offset, dst, byteCount);
        return;
    }

    std::atomic failed(false);
    std::string failure;
    std::mutex failureMutex;

    PXR_NS::WorkParallelForN(
        taskCount,
        [taskCount, &failed, byteCount, offset, dst, &filePath, &failureMutex, &failure](size_t begin, size_t end)
        {
            for (size_t task = begin; task < end; ++task)
            {
                if (failed.load())
                    return;
                const size_t first = (byteCount * task) / taskCount;
                const size_t last = (byteCount * (task + 1)) / taskCount;
                if (first == last)
                    continue;
                try
                {
                    ReadFileRangeInto(filePath, offset + static_cast<uint64_t>(first), dst + first, last - first);
                }
                catch (const std::exception& ex)
                {
                    failed.store(true);
                    std::scoped_lock lock(failureMutex);
                    if (failure.empty())
                        failure = ex.what();
                    return;
                }
            }
        });

    if (failed.load())
        throw cae::FileFormatError(failure.empty() ? "Failed to read VTK file byte range" : failure);
}

PXR_NS::UninitializedVtArray<unsigned char> ReadFileRange(const FileHandle& file, uint64_t offset, size_t byteCount)
{
    PXR_NS::UninitializedVtArray<unsigned char> bytes = PXR_NS::MakeUninitializedVtArray<unsigned char>(byteCount);
    ReadFileRangeIntoParallel(file.GetFilePath(), offset, bytes.data, byteCount, file.GetReadOptions());
    return bytes;
}

int Base64Value(unsigned char c)
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

size_t DecodeBase64Quartet(const unsigned char* encoded, unsigned char decoded[3], bool allowPadding)
{
    std::array<int, 4> values = { 0, 0, 0, 0 };
    size_t padding = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        if (encoded[i] == '=')
        {
            if (!allowPadding || i < 2)
                throw cae::FileFormatError("Malformed VTK base64 payload: invalid padding");
            ++padding;
            continue;
        }
        if (padding != 0)
            throw cae::FileFormatError("Malformed VTK base64 payload: data after padding");
        if (std::isspace(encoded[i]))
            throw cae::FileFormatError("Malformed VTK base64 payload: embedded whitespace");
        values[i] = Base64Value(encoded[i]);
        if (values[i] < 0)
            throw cae::FileFormatError("Malformed VTK base64 payload: invalid character");
    }

    if (padding > 2 || (padding == 1 && encoded[3] != '=') || (padding == 2 && (encoded[2] != '=' || encoded[3] != '=')))
    {
        throw cae::FileFormatError("Malformed VTK base64 payload: invalid padding");
    }

    decoded[0] = static_cast<unsigned char>((values[0] << 2) | (values[1] >> 4));
    decoded[1] = static_cast<unsigned char>(((values[1] & 0x0f) << 4) | (values[2] >> 2));
    decoded[2] = static_cast<unsigned char>(((values[2] & 0x03) << 6) | values[3]);
    return 3 - padding;
}

void DecodeBase64Into(std::string_view text, unsigned char* target, size_t expectedByteCount, bool allowFinalPadding)
{
    if ((text.size() % 4) != 0)
        throw cae::FileFormatError("Malformed VTK base64 payload: encoded byte count is not quartet-aligned");
    CheckTarget(target, expectedByteCount);

    size_t produced = 0;
    for (size_t offset = 0; offset < text.size(); offset += 4)
    {
        const bool isFinalQuartet = offset + 4 == text.size();
        const auto* encoded = reinterpret_cast<const unsigned char*>(text.data() + offset);
        size_t count = 0;

        if (allowFinalPadding && isFinalQuartet)
        {
            std::array<unsigned char, 3> decoded = {};
            count = DecodeBase64Quartet(encoded, decoded.data(), true);
            if (produced > expectedByteCount || count > expectedByteCount - produced)
                throw cae::FileFormatError("Malformed VTK base64 payload: decoded byte count exceeds request");
            std::memcpy(target + produced, decoded.data(), count);
        }
        else
        {
            if (produced > expectedByteCount || 3 > expectedByteCount - produced)
                throw cae::FileFormatError("Malformed VTK base64 payload: decoded byte count exceeds request");
            count = DecodeBase64Quartet(encoded, target + produced, false);
        }

        if (count != 3 && !isFinalQuartet)
            throw cae::FileFormatError("Malformed VTK base64 payload: padding before final quartet");
        produced += count;
    }

    if (produced != expectedByteCount)
        throw cae::FileFormatError("Malformed VTK base64 payload: decoded byte count does not match request");
}

void ReadSegmentFileRangeInto(const FileHandle& file,
                              PayloadSegmentSpec segment,
                              size_t sourceOffset,
                              unsigned char* target,
                              size_t byteCount,
                              size_t sourceByteCount)
{
    if (sourceOffset > sourceByteCount || byteCount > sourceByteCount - sourceOffset)
        throw cae::FileFormatError("VTK payload segment source byte range is out of bounds");
    ReadFileRangeIntoParallel(
        file.GetFilePath(), CheckedOffset(segment.startOffset, sourceOffset), target, byteCount, file.GetReadOptions());
}

PXR_NS::UninitializedVtArray<unsigned char> ReadSegmentFileRange(
    const FileHandle& file, PayloadSegmentSpec segment, size_t sourceOffset, size_t byteCount, size_t sourceByteCount)
{
    PXR_NS::UninitializedVtArray<unsigned char> bytes = PXR_NS::MakeUninitializedVtArray<unsigned char>(byteCount);
    ReadSegmentFileRangeInto(file, segment, sourceOffset, bytes.data, byteCount, sourceByteCount);
    return bytes;
}

size_t DecodedBase64ByteCount(const FileHandle& file, PayloadSegmentSpec segment, size_t sourceByteCount)
{
    if ((sourceByteCount % 4) != 0)
        throw cae::FileFormatError("Malformed VTK base64 payload: encoded byte count is not quartet-aligned");
    if (sourceByteCount == 0)
        return 0;

    std::array<unsigned char, 4> finalQuartet = {};
    ReadSegmentFileRangeInto(
        file, segment, sourceByteCount - 4, finalQuartet.data(), finalQuartet.size(), sourceByteCount);
    std::array<unsigned char, 3> decoded = {};
    const size_t finalDecodedByteCount = DecodeBase64Quartet(finalQuartet.data(), decoded.data(), true);
    const size_t precedingQuartetCount = (sourceByteCount / 4) - 1;
    if (precedingQuartetCount > (std::numeric_limits<size_t>::max() - finalDecodedByteCount) / 3)
        throw std::overflow_error("VTK base64 decoded byte count overflow");
    return precedingQuartetCount * 3 + finalDecodedByteCount;
}

bool IsAsciiWhitespace(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

bool ReadAsciiToken(std::string_view text, size_t* cursor, std::string_view* token)
{
    while (*cursor < text.size() && IsAsciiWhitespace(static_cast<unsigned char>(text[*cursor])))
        ++(*cursor);
    if (*cursor >= text.size())
        return false;

    const size_t begin = *cursor;
    while (*cursor < text.size() && !IsAsciiWhitespace(static_cast<unsigned char>(text[*cursor])))
        ++(*cursor);
    *token = text.substr(begin, *cursor - begin);
    return true;
}

template <typename T>
T ParseAsciiValue(std::string_view token)
{
    if constexpr (std::is_floating_point_v<T>)
    {
        std::string tmp(token);
        errno = 0;
        char* parsedEnd = nullptr;
        const double value = std::strtod(tmp.c_str(), &parsedEnd);
        if (parsedEnd != tmp.c_str() + tmp.size() || errno == ERANGE)
            throw cae::FileFormatError("Failed to parse ASCII VTK floating-point value");
        return static_cast<T>(value);
    }
    else if constexpr (std::is_signed_v<T>)
    {
        std::string tmp(token);
        errno = 0;
        char* parsedEnd = nullptr;
        const long long value = std::strtoll(tmp.c_str(), &parsedEnd, 10);
        if (parsedEnd != tmp.c_str() + tmp.size() || errno == ERANGE)
            throw cae::FileFormatError("Failed to parse ASCII VTK signed integer value");
        if (value < static_cast<long long>(std::numeric_limits<T>::min()) ||
            value > static_cast<long long>(std::numeric_limits<T>::max()))
        {
            throw cae::FileFormatError("ASCII VTK signed integer value is out of range");
        }
        return static_cast<T>(value);
    }
    else
    {
        std::string tmp(token);
        errno = 0;
        char* parsedEnd = nullptr;
        const unsigned long long value = std::strtoull(tmp.c_str(), &parsedEnd, 10);
        if (parsedEnd != tmp.c_str() + tmp.size() || errno == ERANGE)
            throw cae::FileFormatError("Failed to parse ASCII VTK unsigned integer value");
        if (value > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
            throw cae::FileFormatError("ASCII VTK unsigned integer value is out of range");
        return static_cast<T>(value);
    }
}

template <typename T>
void ReadAsciiValues(std::string_view text, size_t skipValues, size_t valueCount, T* values)
{
    size_t cursor = 0;
    for (size_t i = 0; i < skipValues; ++i)
    {
        std::string_view token;
        if (!ReadAsciiToken(text, &cursor, &token))
            throw cae::FileFormatError("ASCII VTK scalar payload has fewer values than expected");
    }

    for (size_t i = 0; i < valueCount; ++i)
    {
        std::string_view token;
        if (!ReadAsciiToken(text, &cursor, &token))
            throw cae::FileFormatError("ASCII VTK scalar payload has fewer values than expected");
        values[i] = ParseAsciiValue<T>(token);
    }

    std::string_view extra;
    if (ReadAsciiToken(text, &cursor, &extra))
        throw cae::FileFormatError("ASCII VTK scalar payload has more values than expected");
}

template <typename T>
T* CheckedTypedTarget(unsigned char* target, size_t byteCount, const char* context)
{
    if (byteCount == 0)
        return nullptr;
    auto* storage = static_cast<void*>(target);
    void* alignedStorage = storage;
    if (size_t alignmentSpace = byteCount; std::align(alignof(T), sizeof(T), alignedStorage, alignmentSpace) != storage)
        throw cae::FileFormatError(std::string(context) + " target is not aligned for scalar type");
    return static_cast<T*>(storage);
}

class RawPayloadSegmentReader final : public PayloadSegmentReader
{
public:
    RawPayloadSegmentReader(const FileHandle& file, PayloadSegmentSpec segment) : _file(file), _segment(segment)
    {
    }

    void ReadInto(unsigned char* target, size_t byteCount, ScalarType scalarType, size_t skipByteCount) const override
    {
        CheckTarget(target, byteCount);
        const size_t sourceByteCount = CheckedSize(_segment.byteCount, "VTK payload segment byte count exceeds size_t");
        CheckDecodedRange(sourceByteCount, skipByteCount, byteCount);
        CheckAdd(_segment.startOffset, skipByteCount);
        ReadFileRangeIntoParallel(
            _file.GetFilePath(), _segment.startOffset + skipByteCount, target, byteCount, _file.GetReadOptions());
        NormalizeScalarBytesInPlace(_file, target, byteCount, scalarType);
    }

private:
    const FileHandle& _file;
    PayloadSegmentSpec _segment;
};

class Base64PayloadSegmentReader final : public PayloadSegmentReader
{
public:
    Base64PayloadSegmentReader(const FileHandle& file, PayloadSegmentSpec segment)
        : _file(file),
          _segment(segment),
          _sourceByteCount(CheckedSize(segment.byteCount, "VTK base64 segment byte count exceeds size_t")),
          _decodedByteCount(DecodedBase64ByteCount(file, segment, _sourceByteCount))
    {
    }

    void ReadInto(unsigned char* target, size_t byteCount, ScalarType scalarType, size_t skipByteCount) const override
    {
        CheckTarget(target, byteCount);
        CheckDecodedRange(_decodedByteCount, skipByteCount, byteCount);
        if (byteCount == 0)
        {
            NormalizeScalarBytesInPlace(_file, target, byteCount, scalarType);
            return;
        }

        size_t decodedCursor = skipByteCount;
        const size_t decodedEnd = decodedCursor + byteCount;
        unsigned char* out = target;

        // Base64 maps each encoded quartet to up to three decoded bytes. The
        // public contract is in decoded bytes, so a requested range may begin
        // or end inside one of those decoded triples. Keep scratch use limited
        // to those boundary triples; the aligned middle decodes directly into
        // the caller's target buffer. This is what lets compressed XML payloads
        // ask for small decoded compressed-byte batches without decoding the
        // entire base64 segment first.
        if ((decodedCursor % 3) != 0)
        {
            std::array<unsigned char, 3> scratch = {};
            const size_t tripleBegin = (decodedCursor / 3) * 3;
            const size_t tripleSize = ReadDecodedTriple(tripleBegin, scratch.data());
            const size_t copyOffset = decodedCursor - tripleBegin;
            if (copyOffset >= tripleSize)
                throw cae::FileFormatError("VTK base64 decoded byte range is out of bounds");
            const size_t copyCount = std::min(decodedEnd, tripleBegin + tripleSize) - decodedCursor;
            std::memcpy(out, scratch.data() + copyOffset, copyCount);
            decodedCursor += copyCount;
            out += copyCount;
        }

        const size_t middleEnd = (decodedEnd / 3) * 3;
        if (middleEnd > decodedCursor)
        {
            ReadAlignedMiddleInto(decodedCursor, middleEnd, out);
            out += middleEnd - decodedCursor;
            decodedCursor = middleEnd;
        }

        if (decodedCursor < decodedEnd)
        {
            std::array<unsigned char, 3> scratch = {};
            const size_t tripleBegin = (decodedCursor / 3) * 3;
            const size_t tripleSize = ReadDecodedTriple(tripleBegin, scratch.data());
            const size_t copyOffset = decodedCursor - tripleBegin;
            const size_t copyCount = decodedEnd - decodedCursor;
            if (copyOffset > tripleSize || copyCount > tripleSize - copyOffset)
                throw cae::FileFormatError("VTK base64 decoded byte range is out of bounds");
            std::memcpy(out, scratch.data() + copyOffset, copyCount);
        }

        NormalizeScalarBytesInPlace(_file, target, byteCount, scalarType);
    }

private:
    void ReadAlignedBatchInto(size_t batch, size_t decodedBegin, size_t decodedEnd, unsigned char* target) const
    {
        // The aligned middle maps whole decoded triples to base64 quartets, so
        // each batch writes a disjoint target slice without padded input.
        const size_t batchDecodedBegin = decodedBegin + batch * Base64DecodedBytesPerBatch;
        const size_t batchDecodedByteCount = std::min(decodedEnd - batchDecodedBegin, Base64DecodedBytesPerBatch);
        const size_t encodedOffset = (batchDecodedBegin / 3) * 4;
        const size_t encodedByteCount = (batchDecodedByteCount / 3) * 4;
        PXR_NS::UninitializedVtArray<unsigned char> encoded = ReadEncodedRange(encodedOffset, encodedByteCount);
        const std::string_view text(reinterpret_cast<const char*>(encoded.data), encodedByteCount);
        DecodeBase64Into(text, target + (batchDecodedBegin - decodedBegin), batchDecodedByteCount, false);
    }

    void ReadAlignedMiddleInto(size_t decodedBegin, size_t decodedEnd, unsigned char* target) const
    {
        if ((decodedBegin % 3) != 0 || (decodedEnd % 3) != 0)
            throw cae::FileFormatError("VTK base64 decoded middle range is not triple-aligned");
        if (decodedBegin >= decodedEnd)
            return;

        const size_t decodedByteCount = decodedEnd - decodedBegin;
        const size_t batchCount = 1 + ((decodedByteCount - 1) / Base64DecodedBytesPerBatch);

        std::atomic failed(false);
        std::mutex failureMutex;
        std::string failure;

        PXR_NS::WorkParallelForN(
            batchCount,
            [this, decodedBegin, decodedEnd, target, &failed, &failureMutex, &failure](size_t begin, size_t end)
            {
                for (size_t batch = begin; batch < end; ++batch)
                {
                    if (failed.load())
                        return;

                    try
                    {
                        ReadAlignedBatchInto(batch, decodedBegin, decodedEnd, target);
                    }
                    catch (const std::exception& ex)
                    {
                        failed.store(true);
                        std::scoped_lock lock(failureMutex);
                        if (failure.empty())
                            failure = ex.what();
                        return;
                    }
                }
            });

        if (failed.load())
            throw cae::FileFormatError(failure.empty() ? "Failed to decode VTK base64 payload" : failure);
    }

    PXR_NS::UninitializedVtArray<unsigned char> ReadEncodedRange(size_t encodedOffset, size_t encodedByteCount) const
    {
        if ((encodedOffset % 4) != 0 || (encodedByteCount % 4) != 0)
            throw cae::FileFormatError("VTK base64 source byte range is not quartet-aligned");
        return ReadSegmentFileRange(_file, _segment, encodedOffset, encodedByteCount, _sourceByteCount);
    }

    size_t ReadDecodedTriple(size_t decodedTripleOffset, unsigned char decoded[3]) const
    {
        if ((decodedTripleOffset % 3) != 0)
            throw cae::FileFormatError("VTK base64 decoded triple offset is not aligned");

        const size_t quartetIndex = decodedTripleOffset / 3;
        if (quartetIndex > std::numeric_limits<size_t>::max() / 4)
            throw std::overflow_error("VTK base64 encoded byte offset overflow");
        const size_t encodedOffset = quartetIndex * 4;
        std::array<unsigned char, 4> encoded = {};
        ReadSegmentFileRangeInto(_file, _segment, encodedOffset, encoded.data(), encoded.size(), _sourceByteCount);
        // Padding is legal only on the segment's final quartet. Earlier
        // quartets must decode to exactly three bytes.
        return DecodeBase64Quartet(encoded.data(), decoded, encodedOffset + 4 == _sourceByteCount);
    }

    const FileHandle& _file;
    PayloadSegmentSpec _segment;
    size_t _sourceByteCount;
    size_t _decodedByteCount;
};

class AsciiPayloadSegmentReader final : public PayloadSegmentReader
{
public:
    AsciiPayloadSegmentReader(const FileHandle& file, PayloadSegmentSpec segment) : _file(file), _segment(segment)
    {
    }

    void ReadInto(unsigned char* target, size_t byteCount, ScalarType scalarType, size_t skipByteCount) const override
    {
        CheckTarget(target, byteCount);
        const size_t scalarSize = ScalarByteSize(scalarType);
        if ((byteCount % scalarSize) != 0 || (skipByteCount % scalarSize) != 0)
            throw cae::FileFormatError("ASCII VTK scalar byte range is not aligned to scalar size");

        const size_t sourceByteCount = CheckedSize(_segment.byteCount, "VTK ASCII segment byte count exceeds size_t");
        PXR_NS::UninitializedVtArray<unsigned char> bytes = ReadFileRange(_file, _segment.startOffset, sourceByteCount);
        const std::string_view text(reinterpret_cast<const char*>(bytes.data), sourceByteCount);
        const size_t skipValues = skipByteCount / scalarSize;
        const size_t valueCount = byteCount / scalarSize;

        switch (scalarType)
        {
        case ScalarType::Int8:
            return ReadAsciiValues<int8_t>(
                text, skipValues, valueCount, CheckedTypedTarget<int8_t>(target, byteCount, "ASCII VTK payload"));
        case ScalarType::UInt8:
            return ReadAsciiValues<unsigned char>(
                text, skipValues, valueCount, CheckedTypedTarget<unsigned char>(target, byteCount, "ASCII VTK payload"));
        case ScalarType::Int16:
            return ReadAsciiValues<int16_t>(
                text, skipValues, valueCount, CheckedTypedTarget<int16_t>(target, byteCount, "ASCII VTK payload"));
        case ScalarType::UInt16:
            return ReadAsciiValues<uint16_t>(
                text, skipValues, valueCount, CheckedTypedTarget<uint16_t>(target, byteCount, "ASCII VTK payload"));
        case ScalarType::Int32:
            return ReadAsciiValues<int32_t>(
                text, skipValues, valueCount, CheckedTypedTarget<int32_t>(target, byteCount, "ASCII VTK payload"));
        case ScalarType::UInt32:
            return ReadAsciiValues<uint32_t>(
                text, skipValues, valueCount, CheckedTypedTarget<uint32_t>(target, byteCount, "ASCII VTK payload"));
        case ScalarType::Int64:
            return ReadAsciiValues<int64_t>(
                text, skipValues, valueCount, CheckedTypedTarget<int64_t>(target, byteCount, "ASCII VTK payload"));
        case ScalarType::UInt64:
            return ReadAsciiValues<uint64_t>(
                text, skipValues, valueCount, CheckedTypedTarget<uint64_t>(target, byteCount, "ASCII VTK payload"));
        case ScalarType::Float32:
            return ReadAsciiValues<float>(
                text, skipValues, valueCount, CheckedTypedTarget<float>(target, byteCount, "ASCII VTK payload"));
        case ScalarType::Float64:
            return ReadAsciiValues<double>(
                text, skipValues, valueCount, CheckedTypedTarget<double>(target, byteCount, "ASCII VTK payload"));
        }
        throw cae::FileFormatError("Unsupported VTK ASCII scalar type");
    }

private:
    const FileHandle& _file;
    PayloadSegmentSpec _segment;
};

} // namespace

FileHandle::FileHandle(FileSpec file, ReadOptions options) : _file(std::move(file)), _options(options)
{
    if (_file.debugContext.empty())
        _file.debugContext = _file.filePath;
}

const std::string& FileHandle::GetFilePath() const
{
    return _file.filePath;
}

ByteOrder FileHandle::GetByteOrder() const
{
    return _file.byteOrder;
}

XmlHeaderType FileHandle::GetXmlHeaderType() const
{
    return _file.xmlHeaderType;
}

XmlCompressor FileHandle::GetXmlCompressor() const
{
    return _file.xmlCompressor;
}

const ReadOptions& FileHandle::GetReadOptions() const
{
    return _options;
}

const std::string& FileHandle::GetDebugName() const
{
    return _file.debugContext;
}

std::unique_ptr<PayloadSegmentReader> MakePayloadSegmentReader(const FileHandle& file,
                                                               StorageKind storageKind,
                                                               PayloadSegmentSpec segment)
{
    switch (storageKind)
    {
    case StorageKind::Ascii:
        return std::make_unique<AsciiPayloadSegmentReader>(file, segment);
    case StorageKind::PlainBinary:
    case StorageKind::XmlBinary:
        return std::make_unique<RawPayloadSegmentReader>(file, segment);
    case StorageKind::XmlBase64Binary:
        return std::make_unique<Base64PayloadSegmentReader>(file, segment);
    }
    throw cae::FileFormatError("Unsupported VTK payload segment storage kind");
}

void NormalizeScalarBytesInPlace(const FileHandle& file, unsigned char* data, size_t byteCount, ScalarType scalarType)
{
    if (byteCount != 0 && !data)
        throw cae::FileFormatError("VTK scalar byte normalization target is null");
    ByteSwapScalarBytesInPlace(data, byteCount, scalarType, file.GetByteOrder(), file.GetReadOptions());
}

} // namespace cae::vtk
