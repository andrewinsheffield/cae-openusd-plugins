// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ScalarPayloadReader.h"

#include "DebugCodes.h"
#include "DisablePXRWarnings.h"
#include "FileFormatError.h"
#include "UninitializedVtArray.h"

#include <lz4.h>
#include <lzma.h>
#include <zlib.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/debug.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/work/loops.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace cae::vtk
{

namespace
{

size_t HeaderWordSize(XmlHeaderType headerType)
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

uint64_t ReadEndianUnsigned(const unsigned char* bytes, size_t byteCount, ByteOrder byteOrder)
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

const char* XmlCompressorName(XmlCompressor compressor)
{
    switch (compressor)
    {
    case XmlCompressor::None:
        return "none";
    case XmlCompressor::ZLib:
        return "zlib";
    case XmlCompressor::Lz4:
        return "lz4";
    case XmlCompressor::Lzma:
        return "lzma";
    }
    return "unknown";
}

size_t CheckedSize(uint64_t value, const char* context)
{
    if (value > std::numeric_limits<size_t>::max())
        throw std::overflow_error(context);
    return value;
}

size_t CheckedAdd(size_t lhs, size_t rhs, const char* context)
{
    if (rhs > std::numeric_limits<size_t>::max() - lhs)
        throw std::overflow_error(context);
    return lhs + rhs;
}

uLong CheckedZlibSize(size_t value)
{
    if (value > std::numeric_limits<uLong>::max())
        throw std::overflow_error("VTK zlib buffer size exceeds uLong");
    return static_cast<uLong>(value); // NOSONAR: required after the checked narrowing conversion.
}

void ValidateDirectTarget(const ScalarPayloadRequest& request,
                          const std::byte* target,
                          size_t targetByteCount,
                          const char* context)
{
    const size_t expectedByteCount = DecodedByteCount(request);
    if (targetByteCount != expectedByteCount)
        throw cae::FileFormatError(std::string(context) + " target byte count does not match scalar payload byte count");
    if (targetByteCount != 0 && !target)
        throw cae::FileFormatError(std::string(context) + " target is null");
}

void ValidateStorageKind(const PayloadSourceSpec& source, StorageKind a, StorageKind b, const char* context)
{
    if (source.storageKind != a && source.storageKind != b)
        throw cae::FileFormatError(std::string(context) + " received an incompatible payload storage kind");
}

void ValidateSegmentCount(const PayloadSourceSpec& source, size_t expected, const char* context)
{
    if (source.segments.size() != expected)
        throw cae::FileFormatError(std::string(context) + " has an unexpected payload segment count");
}

size_t DecodedCompressedSize(size_t numBlocks, size_t blockSize, size_t lastBlockSize)
{
    if (numBlocks == 0)
        return 0;
    if (blockSize == 0)
        throw cae::FileFormatError("VTK XML compressed block size is zero");
    if (lastBlockSize == 0)
    {
        if (numBlocks > std::numeric_limits<size_t>::max() / blockSize)
            throw std::overflow_error("VTK XML decoded compressed payload size overflow");
        return numBlocks * blockSize;
    }
    if (numBlocks - 1 > (std::numeric_limits<size_t>::max() - lastBlockSize) / blockSize)
        throw std::overflow_error("VTK XML decoded compressed payload size overflow");
    return (numBlocks - 1) * blockSize + lastBlockSize;
}

size_t BlockDecodedSize(size_t blockIndex, size_t numBlocks, size_t blockSize, size_t lastBlockSize)
{
    if (blockIndex + 1 < numBlocks || lastBlockSize == 0)
        return blockSize;
    return lastBlockSize;
}

struct XmlCompressedBlockLayout
{
    size_t numBlocks = 0;
    size_t blockSize = 0;
    size_t lastBlockSize = 0;
    size_t headerByteCount = 0;
    size_t compressedByteCount = 0;
    size_t decodedByteCount = 0;
    std::vector<size_t> compressedBlockSizes;
    std::vector<size_t> compressedOffsets;
    std::vector<size_t> decodedOffsets;
};

bool DecompressCompressedBlock(XmlCompressor compressor,
                               const unsigned char* compressedData,
                               size_t compressedSize,
                               unsigned char* decodedData,
                               size_t decodedSize)
{
    switch (compressor)
    {
    case XmlCompressor::Lz4:
    {
        if (compressedSize > static_cast<size_t>(INT_MAX) || decodedSize > static_cast<size_t>(INT_MAX))
            throw cae::FileFormatError("VTK XML LZ4 block is too large for LZ4_decompress_safe");
        const int actual =
            LZ4_decompress_safe(reinterpret_cast<const char*>(compressedData), reinterpret_cast<char*>(decodedData),
                                static_cast<int>(compressedSize), static_cast<int>(decodedSize));
        return actual == static_cast<int>(decodedSize);
    }
    case XmlCompressor::ZLib:
    {
        auto outSize = CheckedZlibSize(decodedSize);
        const int result = uncompress(decodedData, &outSize, compressedData, CheckedZlibSize(compressedSize));
        return result == Z_OK && outSize == CheckedZlibSize(decodedSize);
    }
    case XmlCompressor::Lzma:
    {
        size_t inPos = 0;
        size_t outPos = 0;
        uint64_t memLimit = std::numeric_limits<uint64_t>::max();
        const lzma_ret result = lzma_stream_buffer_decode(
            &memLimit, 0, nullptr, compressedData, &inPos, compressedSize, decodedData, &outPos, decodedSize);
        return result == LZMA_OK && inPos == compressedSize && outPos == decodedSize;
    }
    case XmlCompressor::None:
        break;
    }
    return false;
}

void ReadSegmentInto(const FileHandle& file,
                     const PayloadSourceSpec& source,
                     size_t segmentIndex,
                     unsigned char* target,
                     size_t byteCount,
                     ScalarType scalarType,
                     size_t skipByteCount = 0)
{
    if (segmentIndex >= source.segments.size())
        throw cae::FileFormatError("VTK payload segment index is out of bounds");
    std::unique_ptr<PayloadSegmentReader> reader =
        MakePayloadSegmentReader(file, source.storageKind, source.segments[segmentIndex]);
    reader->ReadInto(target, byteCount, scalarType, skipByteCount);
}

XmlCompressedBlockLayout ReadCompressedXmlBlockLayout(FileHandle& file, const PayloadSourceSpec& source)
{
    ValidateSegmentCount(source, 2, "VTK XML compressed payload");

    const size_t wordSize = HeaderWordSize(file.GetXmlHeaderType());
    std::vector<unsigned char> prefix(wordSize * 3);
    ReadSegmentInto(file, source, 0, prefix.data(), prefix.size(), ScalarType::UInt8);

    XmlCompressedBlockLayout layout;
    layout.numBlocks = CheckedSize(
        ReadEndianUnsigned(prefix.data(), wordSize, file.GetByteOrder()), "VTK XML compressed block count overflow");
    layout.blockSize = CheckedSize(ReadEndianUnsigned(prefix.data() + wordSize, wordSize, file.GetByteOrder()),
                                   "VTK XML compressed block size overflow");
    layout.lastBlockSize = CheckedSize(ReadEndianUnsigned(prefix.data() + 2 * wordSize, wordSize, file.GetByteOrder()),
                                       "VTK XML compressed last block size overflow");
    layout.headerByteCount = wordSize * (3 + layout.numBlocks);
    layout.decodedByteCount = DecodedCompressedSize(layout.numBlocks, layout.blockSize, layout.lastBlockSize);

    if (layout.numBlocks == 0)
        return layout;

    std::vector<unsigned char> header(layout.headerByteCount);
    ReadSegmentInto(file, source, 0, header.data(), header.size(), ScalarType::UInt8);

    layout.compressedBlockSizes.resize(layout.numBlocks);
    layout.compressedOffsets.resize(layout.numBlocks);
    layout.decodedOffsets.resize(layout.numBlocks);

    size_t decodedCursor = 0;
    for (size_t block = 0; block < layout.numBlocks; ++block)
    {
        const size_t entryOffset = wordSize * (3 + block);
        layout.compressedBlockSizes[block] =
            CheckedSize(ReadEndianUnsigned(header.data() + entryOffset, wordSize, file.GetByteOrder()),
                        "VTK XML compressed block byte count overflow");
        layout.compressedOffsets[block] = layout.compressedByteCount;
        layout.compressedByteCount = CheckedAdd(layout.compressedByteCount, layout.compressedBlockSizes[block],
                                                "VTK XML compressed payload byte count overflow");

        layout.decodedOffsets[block] = decodedCursor;
        decodedCursor =
            CheckedAdd(decodedCursor, BlockDecodedSize(block, layout.numBlocks, layout.blockSize, layout.lastBlockSize),
                       "VTK XML decoded payload byte count overflow");
    }
    return layout;
}

void SetFailure(std::atomic<bool>* failed, std::mutex* mutex, std::string* failure, std::string message)
{
    failed->store(true);
    std::scoped_lock lock(*mutex);
    if (failure->empty())
        *failure = std::move(message);
}

void ReadCompressedXmlBlockIntoBatches(FileHandle& file,
                                       const PayloadSourceSpec& source,
                                       const ScalarPayloadRequest& request,
                                       const XmlCompressedBlockLayout& layout,
                                       unsigned char* decoded)
{
    constexpr size_t BlocksPerBatch = 64;
    const size_t batchCount = (layout.numBlocks + BlocksPerBatch - 1) / BlocksPerBatch;

    std::unique_ptr<PayloadSegmentReader> compressedReader =
        MakePayloadSegmentReader(file, source.storageKind, source.segments[1]);
    std::atomic failed(false);
    std::mutex failureMutex;
    std::string failure;

    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg(
            "[VTK] direct decompress XML block file='%s' compressor=%s mode=batched blocks=%zu "
            "blocksPerBatch=%zu compressedBytes=%zu decodedBytes=%zu batches=%zu\n",
            file.GetDebugName().c_str(), XmlCompressorName(file.GetXmlCompressor()), layout.numBlocks, BlocksPerBatch,
            layout.compressedByteCount, layout.decodedByteCount, batchCount);

    PXR_NS::WorkParallelForN(
        batchCount,
        [BlocksPerBatch, batchCount, &failed, &layout, &compressedReader, &file, decoded, &failureMutex, &failure](
            size_t begin, size_t end)
        {
            for (size_t batch = begin; batch < end; ++batch)
            {
                if (failed.load())
                    return;

                const size_t firstBlock = batch * BlocksPerBatch;
                const size_t lastBlock = std::min(layout.numBlocks, firstBlock + BlocksPerBatch);
                const size_t compressedBegin = layout.compressedOffsets[firstBlock];
                const size_t lastCompressedBlock = lastBlock - 1;
                const size_t compressedEnd =
                    layout.compressedOffsets[lastCompressedBlock] + layout.compressedBlockSizes[lastCompressedBlock];
                const size_t batchByteCount = compressedEnd - compressedBegin;

                try
                {
                    PXR_NS::UninitializedVtArray<unsigned char> compressed =
                        PXR_NS::MakeUninitializedVtArray<unsigned char>(batchByteCount);
                    compressedReader->ReadInto(compressed.data, batchByteCount, ScalarType::UInt8, compressedBegin);
                    for (size_t block = firstBlock; block < lastBlock; ++block)
                    {
                        const size_t localCompressedOffset = layout.compressedOffsets[block] - compressedBegin;
                        if (!DecompressCompressedBlock(
                                file.GetXmlCompressor(), compressed.data + localCompressedOffset,
                                layout.compressedBlockSizes[block], decoded + layout.decodedOffsets[block],
                                BlockDecodedSize(block, layout.numBlocks, layout.blockSize, layout.lastBlockSize)))
                        {
                            SetFailure(
                                &failed, &failureMutex, &failure, "Failed to decompress VTK XML compressed payload");
                            return;
                        }
                    }
                }
                catch (const std::exception& ex)
                {
                    SetFailure(&failed, &failureMutex, &failure, ex.what());
                    return;
                }
            }
        });

    if (failed.load())
        throw cae::FileFormatError(failure.empty() ? "Failed to direct-decompress VTK XML payload" : failure);

    NormalizeScalarBytesInPlace(file, decoded, layout.decodedByteCount, request.scalarType);
}

void ReadCompressedXmlBlockInto(FileHandle& file,
                                const PayloadSourceSpec& source,
                                const ScalarPayloadRequest& request,
                                unsigned char* decoded,
                                size_t decodedByteCount)
{
    const XmlCompressedBlockLayout layout = ReadCompressedXmlBlockLayout(file, source);
    if (layout.decodedByteCount != decodedByteCount)
        throw cae::FileFormatError("VTK XML direct compressed target size does not match decoded payload size");
    if (layout.numBlocks == 0)
        return;

    ReadCompressedXmlBlockIntoBatches(file, source, request, layout, decoded);
}

class ScalarByteSource final : public PXR_NS::Vt_ArrayForeignDataSource
{
public:
    explicit ScalarByteSource(PXR_NS::VtUCharArray bytes)
        : PXR_NS::Vt_ArrayForeignDataSource(&ScalarByteSource::Detached), _bytes(std::move(bytes))
    {
    }

    unsigned char* Data()
    {
        return _bytes.data();
    }

    static void Detached(PXR_NS::Vt_ArrayForeignDataSource* self)
    {
        std::default_delete<ScalarByteSource>{}(static_cast<ScalarByteSource*>(self));
    }

private:
    PXR_NS::VtUCharArray _bytes;
};

template <typename T>
PXR_NS::VtValue MakeTypedArrayValue(PXR_NS::VtUCharArray bytes, const std::string& debugContext)
{
    if ((bytes.size() % sizeof(T)) != 0)
        throw cae::FileFormatError("VTK scalar payload byte count is not divisible by target scalar size");

    const size_t count = bytes.size() / sizeof(T);
    if (count == 0)
        return PXR_NS::VtValue(PXR_NS::VtArray<T>());

    auto* storage = static_cast<void*>(bytes.data());
    void* alignedStorage = storage;
    if (size_t alignmentSpace = bytes.size(); std::align(alignof(T), sizeof(T), alignedStorage, alignmentSpace) == storage)
    {
        auto source = std::make_unique<ScalarByteSource>(std::move(bytes));
        PXR_NS::VtArray<T> values(source.get(), static_cast<T*>(static_cast<void*>(source->Data())), count);
        source.release();
        return PXR_NS::VtValue::Take(values);
    }

    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] copy scalar payload due to alignment debug='%s' address=%p align=%zu bytes=%zu\n",
             debugContext.c_str(), static_cast<const void*>(bytes.cdata()), alignof(T), bytes.size());

    PXR_NS::VtArray<T> values(count);
    std::memcpy(values.data(), bytes.cdata(), bytes.size());
    return PXR_NS::VtValue::Take(values);
}

template <typename T>
PXR_NS::VtValue ReadDataAs(const ScalarPayloadReader& reader, FileHandle& file, const ScalarPayloadRequest& request)
{
    PXR_NS::UninitializedVtArray<T> values = PXR_NS::MakeUninitializedVtArray<T>(request.valueCount);
    if (values.data)
    {
        reader.ReadDataInto(
            file, request, static_cast<std::byte*>(static_cast<void*>(values.data)), DecodedByteCount(request));
    }
    return PXR_NS::VtValue::Take(values.array);
}

} // namespace

PXR_NS::VtValue ScalarPayloadReader::ReadData(FileHandle& file, const ScalarPayloadRequest& request) const
{
    switch (request.scalarType)
    {
    case ScalarType::Int8:
        return ReadDataAs<int8_t>(*this, file, request);
    case ScalarType::UInt8:
        return ReadDataAs<unsigned char>(*this, file, request);
    case ScalarType::Int16:
        return ReadDataAs<int16_t>(*this, file, request);
    case ScalarType::UInt16:
        return ReadDataAs<uint16_t>(*this, file, request);
    case ScalarType::Int32:
        return ReadDataAs<int32_t>(*this, file, request);
    case ScalarType::UInt32:
        return ReadDataAs<uint32_t>(*this, file, request);
    case ScalarType::Int64:
        return ReadDataAs<int64_t>(*this, file, request);
    case ScalarType::UInt64:
        return ReadDataAs<uint64_t>(*this, file, request);
    case ScalarType::Float32:
        return ReadDataAs<float>(*this, file, request);
    case ScalarType::Float64:
        return ReadDataAs<double>(*this, file, request);
    }
    throw cae::FileFormatError("Unsupported VTK scalar type");
}

DirectScalarPayloadReader::DirectScalarPayloadReader(PayloadSourceSpec source) : _source(std::move(source))
{
    ValidateStorageKind(_source, StorageKind::Ascii, StorageKind::PlainBinary, "Direct VTK scalar payload");
}

void DirectScalarPayloadReader::ReadDataInto(FileHandle& file,
                                             const ScalarPayloadRequest& request,
                                             std::byte* target,
                                             size_t targetByteCount) const
{
    ValidateDirectTarget(request, target, targetByteCount, "Direct VTK scalar payload");
    ValidateSegmentCount(_source, 1, "Direct VTK scalar payload");
    ReadSegmentInto(
        file, _source, 0, static_cast<unsigned char*>(static_cast<void*>(target)), targetByteCount, request.scalarType);
}

XmlBinaryBlockPayloadReader::XmlBinaryBlockPayloadReader(PayloadSourceSpec source) : _source(std::move(source))
{
    ValidateStorageKind(_source, StorageKind::XmlBinary, StorageKind::XmlBase64Binary, "VTK XML binary block payload");
}

void XmlBinaryBlockPayloadReader::ReadDataInto(FileHandle& file,
                                               const ScalarPayloadRequest& request,
                                               std::byte* target,
                                               size_t targetByteCount) const
{
    ValidateDirectTarget(request, target, targetByteCount, "VTK XML binary block payload");
    // zlib and the base64 decoder expose unsigned-char byte APIs.
    auto* bytes = static_cast<unsigned char*>(static_cast<void*>(target));

    if (file.GetXmlCompressor() != XmlCompressor::None)
    {
        ReadCompressedXmlBlockInto(file, _source, request, bytes, targetByteCount);
        return;
    }

    ValidateSegmentCount(_source, 1, "VTK XML binary block payload");
    const size_t wordSize = HeaderWordSize(file.GetXmlHeaderType());
    std::vector<unsigned char> header(wordSize);
    ReadSegmentInto(file, _source, 0, header.data(), header.size(), ScalarType::UInt8);
    const uint64_t payloadByteCount = ReadEndianUnsigned(header.data(), wordSize, file.GetByteOrder());
    if (payloadByteCount != targetByteCount)
        throw cae::FileFormatError("VTK XML binary block payload size does not match direct target size");
    if (payloadByteCount > std::numeric_limits<size_t>::max())
        throw std::overflow_error("VTK XML binary block payload size exceeds size_t");

    ReadSegmentInto(file, _source, 0, bytes, payloadByteCount, request.scalarType, wordSize);
}

PXR_NS::VtValue MakeTypedScalarValue(PXR_NS::VtUCharArray bytes, ScalarType scalarType, const std::string& debugContext)
{
    switch (scalarType)
    {
    case ScalarType::Int8:
        return MakeTypedArrayValue<int8_t>(std::move(bytes), debugContext);
    case ScalarType::UInt8:
        return MakeTypedArrayValue<unsigned char>(std::move(bytes), debugContext);
    case ScalarType::Int16:
        return MakeTypedArrayValue<int16_t>(std::move(bytes), debugContext);
    case ScalarType::UInt16:
        return MakeTypedArrayValue<uint16_t>(std::move(bytes), debugContext);
    case ScalarType::Int32:
        return MakeTypedArrayValue<int32_t>(std::move(bytes), debugContext);
    case ScalarType::UInt32:
        return MakeTypedArrayValue<uint32_t>(std::move(bytes), debugContext);
    case ScalarType::Int64:
        return MakeTypedArrayValue<int64_t>(std::move(bytes), debugContext);
    case ScalarType::UInt64:
        return MakeTypedArrayValue<uint64_t>(std::move(bytes), debugContext);
    case ScalarType::Float32:
        return MakeTypedArrayValue<float>(std::move(bytes), debugContext);
    case ScalarType::Float64:
        return MakeTypedArrayValue<double>(std::move(bytes), debugContext);
    }
    throw cae::FileFormatError("Unsupported VTK scalar type");
}

} // namespace cae::vtk
