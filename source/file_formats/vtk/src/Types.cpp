// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Types.h"

#include "DisablePXRWarnings.h"
#include "FileFormatError.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/diagnostic.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cae::vtk
{

namespace
{

bool HostIsLittleEndian()
{
    const uint16_t value = 1;
    return *reinterpret_cast<const uint8_t*>(&value) == 1;
}

} // namespace

size_t ScalarByteSize(ScalarType scalarType)
{
    switch (scalarType)
    {
    case ScalarType::Int8:
    case ScalarType::UInt8:
        return 1;
    case ScalarType::Int16:
    case ScalarType::UInt16:
        return 2;
    case ScalarType::Int32:
    case ScalarType::UInt32:
    case ScalarType::Float32:
        return 4;
    case ScalarType::Int64:
    case ScalarType::UInt64:
    case ScalarType::Float64:
        return 8;
    }
    throw cae::FileFormatError("Unsupported VTK scalar type");
}

bool ByteOrderMatchesHost(ByteOrder byteOrder)
{
    return (byteOrder == ByteOrder::LittleEndian) == HostIsLittleEndian();
}

size_t DecodedByteCount(const ScalarPayloadRequest& request)
{
    const size_t scalarSize = ScalarByteSize(request.scalarType);
    if (request.valueCount > std::numeric_limits<size_t>::max() / scalarSize)
        throw std::overflow_error("VTK scalar payload byte count overflow");
    return request.valueCount * scalarSize;
}

void ByteSwapScalarBytesInPlace(
    uint8_t* data, size_t byteCount, ScalarType scalarType, ByteOrder sourceByteOrder, const ReadOptions& /*options*/)
{
    const size_t scalarSize = ScalarByteSize(scalarType);
    if (scalarSize <= 1 || byteCount == 0 || ByteOrderMatchesHost(sourceByteOrder))
        return;
    if ((byteCount % scalarSize) != 0)
        throw cae::FileFormatError("VTK scalar payload byte count is not divisible by scalar size");

    const size_t valueCount = byteCount / scalarSize;
    for (size_t i = 0; i < valueCount; ++i)
    {
        uint8_t* first = data + i * scalarSize;
        std::reverse(first, first + scalarSize);
    }
}

} // namespace cae::vtk
