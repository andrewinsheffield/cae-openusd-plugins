// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file Types.h
///
/// Source-private common types for the VTK file format. These types describe
/// scalar payloads without committing to legacy VTK, VTK XML, or USD authoring
/// details.

#include "DisablePXRWarnings.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <cstddef>
#include <cstdint>

namespace cae::vtk
{

/// Numeric scalar types supported by the VTK reader.
enum class ScalarType
{
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float32,
    Float64,
};

/// Byte order used by binary scalar payloads and XML block headers.
enum class ByteOrder
{
    LittleEndian,
    BigEndian,
};

/// VTK XML binary-block header word width.
enum class XmlHeaderType
{
    UInt32,
    UInt64,
};

/// VTK XML compressor declared on the root VTKFile element.
enum class XmlCompressor
{
    None,
    ZLib,
    Lz4,
    Lzma,
};

/// Per-read options that should not affect parsed metadata cache identity.
struct ReadOptions
{
    int ioThreads = 1;
};

/// Request for one flat scalar payload.
///
/// `valueCount` is the number of scalar values, not tuples. For component
/// arrays, callers pass tupleCount * componentCount.
struct ScalarPayloadRequest
{
    ScalarType scalarType = ScalarType::Float32;
    size_t valueCount = 0;
};

/// Return the size in bytes of one scalar value.
size_t ScalarByteSize(ScalarType scalarType);

/// Return true when `byteOrder` matches this host.
bool ByteOrderMatchesHost(ByteOrder byteOrder);

/// Return decoded byte count for a payload request, with overflow checks in
/// the implementation.
size_t DecodedByteCount(const ScalarPayloadRequest& request);

/// Byte-swap scalar bytes in place.
///
/// One-byte scalar types and host-endian sources are no-ops. Wider scalar
/// payloads must have a byte count divisible by `ScalarByteSize(scalarType)`.
/// `options` is part of the contract so this utility can gain internal
/// parallelism without changing callers.
void ByteSwapScalarBytesInPlace(
    uint8_t* data, size_t byteCount, ScalarType scalarType, ByteOrder sourceByteOrder, const ReadOptions& options);

} // namespace cae::vtk
