// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file FileHandle.h
///
/// Runtime file context and payload segment readers used by scalar payload
/// readers. FileHandle owns file-level metadata only; actual byte reads happen
/// through PayloadSegmentReader implementations.

#include "DatasetSpec.h"
#include "DisablePXRWarnings.h"
#include "Types.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/pxr.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace cae::vtk
{

/// Shared immutable runtime context for one VTK file.
class FileHandle
{
public:
    FileHandle(FileSpec file, ReadOptions options);

    const std::string& GetFilePath() const;
    ByteOrder GetByteOrder() const;
    XmlHeaderType GetXmlHeaderType() const;
    XmlCompressor GetXmlCompressor() const;
    const ReadOptions& GetReadOptions() const;
    const std::string& GetDebugName() const;

private:
    FileSpec _file;
    ReadOptions _options;
};

/// Reads one payload source segment into decoded/unencoded bytes.
class PayloadSegmentReader
{
public:
    virtual ~PayloadSegmentReader() = default;

    /// Fill `target` with `byteCount` bytes from this segment's decoded byte
    /// stream, skipping `skipByteCount` decoded bytes first.
    ///
    /// `ScalarType::UInt8` requests uninterpreted bytes. Wider scalar types are
    /// normalized to native byte order by this layer.
    virtual void ReadInto(unsigned char* target,
                          size_t byteCount,
                          ScalarType scalarType,
                          size_t skipByteCount = 0) const = 0;
};

/// Create a segment reader for a payload segment and storage kind.
std::unique_ptr<PayloadSegmentReader> MakePayloadSegmentReader(const FileHandle& file,
                                                               StorageKind storageKind,
                                                               PayloadSegmentSpec segment);

/// Normalize scalar bytes according to the file byte order.
void NormalizeScalarBytesInPlace(const FileHandle& file, unsigned char* data, size_t byteCount, ScalarType scalarType);

} // namespace cae::vtk
