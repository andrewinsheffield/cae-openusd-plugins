// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "EclipseBinaryFile.h"

#include "FileFormatError.h"
#include "UninitializedVtArray.h"
#include "debugCodes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{

uint32_t ReadUInt32(const char* bytes, CaeEclipseEndian endian)
{
    const auto* data = reinterpret_cast<const unsigned char*>(bytes);
    if (endian == CaeEclipseEndian::Big)
    {
        return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
               (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
    }

    return (static_cast<uint32_t>(data[3]) << 24) | (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) | static_cast<uint32_t>(data[0]);
}

uint64_t ReadUInt64(const char* bytes, CaeEclipseEndian endian)
{
    const auto* data = reinterpret_cast<const unsigned char*>(bytes);
    uint64_t value = 0;
    if (endian == CaeEclipseEndian::Big)
    {
        for (size_t i = 0; i < 8; ++i)
            value = (value << 8) | static_cast<uint64_t>(data[i]);
    }
    else
    {
        for (size_t i = 0; i < 8; ++i)
            value = (value << 8) | static_cast<uint64_t>(data[7 - i]);
    }
    return value;
}

int32_t ReadInt32(const char* bytes, CaeEclipseEndian endian)
{
    const uint32_t value = ReadUInt32(bytes, endian);
    int32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

float ReadFloat32(const char* bytes, CaeEclipseEndian endian)
{
    const uint32_t bits = ReadUInt32(bytes, endian);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double ReadFloat64(const char* bytes, CaeEclipseEndian endian)
{
    const uint64_t bits = ReadUInt64(bytes, endian);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool ReadExact(std::istream& input, char* data, size_t byteCount)
{
    if (byteCount > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()))
        return false;
    input.read(data, static_cast<std::streamsize>(byteCount));
    return input.good() || (input.eof() && static_cast<size_t>(input.gcount()) == byteCount);
}

bool ReadRecordLength(std::istream& input, CaeEclipseEndian endian, int32_t* value)
{
    std::array<char, 4> bytes = {};
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() == 0 && input.eof())
        return false;
    if (input.gcount() != static_cast<std::streamsize>(sizeof(bytes)))
        throw cae::FileFormatError("Unexpected end of Eclipse binary file while reading a record marker.");
    *value = ReadInt32(bytes.data(), endian);
    return true;
}

uint64_t Tell(std::istream& input)
{
    const std::streampos pos = input.tellg();
    if (pos < 0)
        throw cae::FileFormatError("Failed to determine Eclipse binary stream position.");
    return static_cast<uint64_t>(pos);
}

void SeekForward(std::istream& input, uint64_t byteCount)
{
    if (byteCount > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw cae::FileFormatError("Eclipse binary record is too large to skip.");
    input.seekg(static_cast<std::streamoff>(byteCount), std::ios::cur);
    if (!input)
        throw cae::FileFormatError("Unexpected end of Eclipse binary file while skipping record data.");
}

size_t TypeItemSize(const std::string& type)
{
    if (type == "INTE" || type == "LOGI" || type == "REAL")
        return 4;
    if (type == "DOUB" || type == "CHAR")
        return 8;
    throw cae::FileFormatError("Unsupported Eclipse binary record type '" + type + "'.");
}

const char* EndianName(CaeEclipseEndian endian)
{
    return endian == CaeEclipseEndian::Big ? "big" : "little";
}

uint64_t ExpectedDataBytes(size_t count, const std::string& type)
{
    // Eclipse restart files use zero-count marker records such as STARTSOL and
    // ENDSOL with non-array type tags like MESS.  They carry no payload, so they
    // should be indexable even though they are not value-loadable array types.
    if (count == 0)
        return 0;

    const size_t itemSize = TypeItemSize(type);
    if (count > std::numeric_limits<uint64_t>::max() / itemSize)
        throw cae::FileFormatError("Eclipse binary record byte count overflow.");
    return count * itemSize;
}

size_t CheckedChunkByteCount(const CaeEclipseDataChunk& chunk)
{
    if (chunk.byteCount > std::numeric_limits<size_t>::max())
        throw cae::FileFormatError("Eclipse binary data chunk is too large to load.");
    return chunk.byteCount;
}

void ReadRecordBytes(const CaeEclipseRecord& record, const CaeEclipseDataChunk& chunk, char* bytes, size_t byteCount)
{
    if (CheckedChunkByteCount(chunk) != byteCount)
        throw cae::FileFormatError("Eclipse binary data chunk size mismatch.");
    if (byteCount == 0)
        return;

    std::ifstream input(record.filePath, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open Eclipse binary file: " + record.filePath);
    if (chunk.offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw cae::FileFormatError("Eclipse binary data chunk offset is too large.");

    input.seekg(static_cast<std::streamoff>(chunk.offset), std::ios::beg);
    if (!input)
        throw cae::FileFormatError("Failed to seek to Eclipse binary data chunk for " + record.keyword);

    if (!ReadExact(input, bytes, byteCount))
        throw cae::FileFormatError("Unexpected end of Eclipse binary file while loading " + record.keyword);
}

void ReadRecordBytes(const CaeEclipseRecord& record, const CaeEclipseDataChunk& chunk, std::vector<char>* bytes)
{
    if (!bytes)
        return;

    bytes->resize(CheckedChunkByteCount(chunk));
    ReadRecordBytes(record, chunk, bytes->data(), bytes->size());
}

} // namespace

std::string CaeEclipseTrimRight(std::string value)
{
    while (!value.empty())
    {
        if (const auto c = static_cast<unsigned char>(value.back()); c != '\0' && !std::isspace(c))
            break;
        value.pop_back();
    }
    return value;
}

std::string CaeEclipseToUpper(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string CaeEclipseToLower(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::optional<CaeEclipseEndian> CaeDetectEclipseEndian(const std::string& filePath)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        return std::nullopt;

    std::array<char, 4> bytes = {};
    if (!ReadExact(input, bytes.data(), bytes.size()))
        return std::nullopt;

    if (ReadInt32(bytes.data(), CaeEclipseEndian::Big) == 16)
        return CaeEclipseEndian::Big;
    if (ReadInt32(bytes.data(), CaeEclipseEndian::Little) == 16)
        return CaeEclipseEndian::Little;
    return std::nullopt;
}

bool CaeReadFirstEclipseRecordHeader(const std::string& filePath, std::string* keyword, std::string* type)
{
    const std::optional<CaeEclipseEndian> endian = CaeDetectEclipseEndian(filePath);
    if (!endian)
        return false;

    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        return false;

    int32_t headerLength = 0;
    if (!ReadRecordLength(input, *endian, &headerLength) || headerLength != 16)
        return false;

    std::array<char, 16> header = {};
    if (!ReadExact(input, header.data(), header.size()))
        return false;

    if (keyword)
        *keyword = CaeEclipseTrimRight(std::string(header.data(), header.data() + 8));
    if (type)
        *type = CaeEclipseTrimRight(std::string(header.data() + 12, header.data() + 16));
    return true;
}

static void ReadEclipseRecordChunks(std::istream& input, CaeEclipseRecord* record)
{
    const uint64_t expectedBytes = ExpectedDataBytes(record->count, record->type);
    uint64_t bytesRead = 0;
    while (bytesRead < expectedBytes)
    {
        int32_t dataLength = 0;
        if (!ReadRecordLength(input, record->endian, &dataLength))
            throw cae::FileFormatError("Unexpected end of Eclipse binary file before record data for " + record->keyword);
        if (dataLength < 0)
            throw cae::FileFormatError("Negative Eclipse binary data record marker for " + record->keyword);

        const auto chunkBytes = static_cast<uint64_t>(dataLength);
        if (bytesRead + chunkBytes > expectedBytes)
            throw cae::FileFormatError("Eclipse binary record " + record->keyword +
                                       " contains more data than its header declares.");

        record->chunks.push_back({ Tell(input), chunkBytes });
        SeekForward(input, chunkBytes);

        if (int32_t endDataLength = 0;
            !ReadRecordLength(input, record->endian, &endDataLength) || endDataLength != dataLength)
            throw cae::FileFormatError("Mismatched Eclipse binary data record markers for " + record->keyword);
        bytesRead += chunkBytes;
    }
}

static CaeEclipseRecord ReadIndexedEclipseRecord(std::istream& input,
                                                 std::string_view filePath,
                                                 CaeEclipseEndian endian,
                                                 int32_t headerLength)
{
    if (headerLength != 16)
        throw cae::FileFormatError("Expected 16-byte Eclipse binary item header, got " + std::to_string(headerLength));

    std::array<char, 16> header = {};
    if (!ReadExact(input, header.data(), header.size()))
        throw cae::FileFormatError("Unexpected end of Eclipse binary file while reading item header.");

    if (int32_t endHeaderLength = 0; !ReadRecordLength(input, endian, &endHeaderLength) || endHeaderLength != headerLength)
        throw cae::FileFormatError("Mismatched Eclipse binary item header record markers.");

    CaeEclipseRecord record;
    record.keyword = CaeEclipseTrimRight(std::string(header.data(), header.data() + 8));
    const int32_t count = ReadInt32(header.data() + 8, endian);
    if (count < 0)
        throw cae::FileFormatError("Negative Eclipse binary item count for " + record.keyword);
    record.count = static_cast<size_t>(count);
    record.type = CaeEclipseTrimRight(std::string(header.data() + 12, header.data() + 16));
    record.filePath = std::string(filePath);
    record.endian = endian;
    ReadEclipseRecordChunks(input, &record);
    return record;
}

CaeEclipseFileIndex CaeIndexEclipseBinaryFile(const std::string& filePath, const std::string& stopAfterKeyword)
{
    const std::optional<CaeEclipseEndian> endian = CaeDetectEclipseEndian(filePath);
    if (!endian)
        throw cae::FileFormatError("Eclipse binary file does not start with a 16-byte item header.");

    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open Eclipse binary file: " + filePath);

    CaeEclipseFileIndex index;
    index.filePath = filePath;
    index.endian = *endian;
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[EclipseBinary] indexing '%s' endian=%s stopAfter='%s'\n", filePath.c_str(), EndianName(index.endian),
             stopAfterKeyword.c_str());

    int32_t headerLength = 0;
    while (ReadRecordLength(input, index.endian, &headerLength))
    {
        // Store chunk offsets instead of payload values.  This keeps metadata
        // reads cheap and lets CaeFileFormatData load large records on demand.
        CaeEclipseRecord record = ReadIndexedEclipseRecord(input, filePath, index.endian, headerLength);
        const bool stop = (!stopAfterKeyword.empty() && record.keyword == stopAfterKeyword);
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("[EclipseBinary] record keyword='%s' type='%s' count=%zu chunks=%zu stop=%d\n", record.keyword.c_str(),
                 record.type.c_str(), record.count, record.chunks.size(), stop ? 1 : 0);
        index.records.push_back(std::move(record));
        if (stop)
            break;
    }

    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[EclipseBinary] indexed %zu record(s) from '%s'\n", index.records.size(), filePath.c_str());
    return index;
}

const CaeEclipseRecord* CaeFindFirstEclipseRecord(const CaeEclipseFileIndex& index,
                                                  std::string_view keyword,
                                                  std::string_view stopBeforeKeyword)
{
    for (const CaeEclipseRecord& record : index.records)
    {
        if (!stopBeforeKeyword.empty() && record.keyword == stopBeforeKeyword)
            break;
        if (record.keyword == keyword)
            return &record;
    }
    return nullptr;
}

bool CaeEclipseIsIntegerRecord(const CaeEclipseRecord& record)
{
    return record.type == "INTE" || record.type == "LOGI";
}

bool CaeEclipseIsFloatingPointRecord(const CaeEclipseRecord& record)
{
    return record.type == "REAL" || record.type == "DOUB";
}

bool CaeEclipseIsNumericRecord(const CaeEclipseRecord& record)
{
    return CaeEclipseIsIntegerRecord(record) || CaeEclipseIsFloatingPointRecord(record);
}

std::vector<int> CaeLoadEclipseIntValues(const CaeEclipseRecord& record)
{
    if (!CaeEclipseIsIntegerRecord(record))
        throw cae::FileFormatError("Eclipse binary record " + record.keyword + " is not an integer record.");

    std::vector<int> values;
    values.reserve(record.count);
    std::vector<char> bytes;
    for (const CaeEclipseDataChunk& chunk : record.chunks)
    {
        if (chunk.byteCount % 4 != 0)
            throw cae::FileFormatError("Eclipse binary integer record " + record.keyword + " has a partial value chunk.");
        ReadRecordBytes(record, chunk, &bytes);
        for (size_t offset = 0; offset < bytes.size(); offset += 4)
            values.push_back(ReadInt32(bytes.data() + offset, record.endian));
    }
    if (values.size() != record.count)
        throw cae::FileFormatError("Eclipse binary integer record " + record.keyword + " count mismatch.");
    return values;
}

VtIntArray CaeLoadEclipseIntArray(const CaeEclipseRecord& record)
{
    if (!CaeEclipseIsIntegerRecord(record))
        throw cae::FileFormatError("Eclipse binary record " + record.keyword + " is not an integer record.");

    UninitializedVtArray<int> result = MakeUninitializedVtArray<int>(record.count);
    size_t index = 0;
    std::unique_ptr<char[]> bytes;
    size_t byteCapacity = 0;
    for (const CaeEclipseDataChunk& chunk : record.chunks)
    {
        if (chunk.byteCount % 4 != 0)
            throw cae::FileFormatError("Eclipse binary integer record " + record.keyword + " has a partial value chunk.");
        const size_t byteCount = CheckedChunkByteCount(chunk);
        if (byteCount > byteCapacity)
        {
            bytes.reset(new char[byteCount]);
            byteCapacity = byteCount;
        }
        ReadRecordBytes(record, chunk, bytes.get(), byteCount);
        for (size_t offset = 0; offset < byteCount; offset += 4)
        {
            if (index >= result.array.size())
                throw cae::FileFormatError("Eclipse binary integer record " + record.keyword + " count mismatch.");
            result.data[index++] = static_cast<int>(ReadInt32(bytes.get() + offset, record.endian));
        }
    }
    if (index != result.array.size())
        throw cae::FileFormatError("Eclipse binary integer record " + record.keyword + " count mismatch.");
    return std::move(result.array);
}

std::vector<double> CaeLoadEclipseDoubleValues(const CaeEclipseRecord& record)
{
    if (!CaeEclipseIsFloatingPointRecord(record))
        throw cae::FileFormatError("Eclipse binary record " + record.keyword + " is not a floating-point record.");

    const size_t itemSize = (record.type == "REAL") ? 4 : 8;
    std::vector<double> values;
    values.reserve(record.count);
    std::vector<char> bytes;
    for (const CaeEclipseDataChunk& chunk : record.chunks)
    {
        if (chunk.byteCount % itemSize != 0)
        {
            throw cae::FileFormatError("Eclipse binary floating-point record " + record.keyword +
                                       " has a partial value chunk.");
        }
        ReadRecordBytes(record, chunk, &bytes);
        for (size_t offset = 0; offset < bytes.size(); offset += itemSize)
        {
            values.push_back(record.type == "REAL" ?
                                 static_cast<double>(ReadFloat32(bytes.data() + offset, record.endian)) :
                                 ReadFloat64(bytes.data() + offset, record.endian));
        }
    }
    if (values.size() != record.count)
        throw cae::FileFormatError("Eclipse binary floating-point record " + record.keyword + " count mismatch.");
    return values;
}

VtDoubleArray CaeLoadEclipseDoubleArray(const CaeEclipseRecord& record)
{
    if (!CaeEclipseIsFloatingPointRecord(record))
        throw cae::FileFormatError("Eclipse binary record " + record.keyword + " is not a floating-point record.");

    const size_t itemSize = (record.type == "REAL") ? 4 : 8;
    UninitializedVtArray<double> result = MakeUninitializedVtArray<double>(record.count);
    size_t index = 0;
    std::unique_ptr<char[]> bytes;
    size_t byteCapacity = 0;
    for (const CaeEclipseDataChunk& chunk : record.chunks)
    {
        if (chunk.byteCount % itemSize != 0)
        {
            throw cae::FileFormatError("Eclipse binary floating-point record " + record.keyword +
                                       " has a partial value chunk.");
        }
        const size_t byteCount = CheckedChunkByteCount(chunk);
        if (byteCount > byteCapacity)
        {
            bytes.reset(new char[byteCount]);
            byteCapacity = byteCount;
        }
        ReadRecordBytes(record, chunk, bytes.get(), byteCount);
        for (size_t offset = 0; offset < byteCount; offset += itemSize)
        {
            if (index >= result.array.size())
                throw cae::FileFormatError("Eclipse binary floating-point record " + record.keyword + " count mismatch.");
            result.data[index++] = record.type == "REAL" ?
                                       static_cast<double>(ReadFloat32(bytes.get() + offset, record.endian)) :
                                       ReadFloat64(bytes.get() + offset, record.endian);
        }
    }
    if (index != result.array.size())
        throw cae::FileFormatError("Eclipse binary floating-point record " + record.keyword + " count mismatch.");
    return std::move(result.array);
}

std::vector<std::string> CaeLoadEclipseCharValues(const CaeEclipseRecord& record)
{
    if (record.type != "CHAR")
        throw cae::FileFormatError("Eclipse binary record " + record.keyword + " is not a character record.");

    std::vector<std::string> values;
    values.reserve(record.count);
    std::vector<char> bytes;
    for (const CaeEclipseDataChunk& chunk : record.chunks)
    {
        if (chunk.byteCount % 8 != 0)
            throw cae::FileFormatError("Eclipse binary character record " + record.keyword +
                                       " has a partial value chunk.");
        ReadRecordBytes(record, chunk, &bytes);
        for (size_t offset = 0; offset < bytes.size(); offset += 8)
            values.push_back(CaeEclipseTrimRight(std::string(bytes.data() + offset, bytes.data() + offset + 8)));
    }
    if (values.size() != record.count)
        throw cae::FileFormatError("Eclipse binary character record " + record.keyword + " count mismatch.");
    return values;
}

PXR_NAMESPACE_CLOSE_SCOPE
