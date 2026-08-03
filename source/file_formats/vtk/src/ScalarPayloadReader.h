// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file ScalarPayloadReader.h
///
/// Readers that turn stored scalar payloads into typed `VtValue` arrays.

#include "DisablePXRWarnings.h"
#include "FileHandle.h"
#include "Types.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <cstddef>
#include <string>

namespace cae::vtk
{

/// Base class for per-payload scalar readers.
class ScalarPayloadReader
{
public:
    virtual ~ScalarPayloadReader() = default;

    /// Allocate a typed scalar array, fill it through `ReadDataInto()`, and
    /// return it in a VtValue.
    PXR_NS::VtValue ReadData(FileHandle& file, const ScalarPayloadRequest& request) const;

    /// Read one scalar payload into caller-owned native-endian storage.
    ///
    /// `targetByteCount` must equal `DecodedByteCount(request)`. The target
    /// storage must be writable and suitably aligned for `request.scalarType`.
    virtual void ReadDataInto(FileHandle& file,
                              const ScalarPayloadRequest& request,
                              std::byte* target,
                              size_t targetByteCount) const = 0;
};

/// Reads direct scalar data from one ASCII or plain-binary segment.
class DirectScalarPayloadReader final : public ScalarPayloadReader
{
public:
    explicit DirectScalarPayloadReader(PayloadSourceSpec source);

    void ReadDataInto(FileHandle& file,
                      const ScalarPayloadRequest& request,
                      std::byte* target,
                      size_t targetByteCount) const override;

private:
    PayloadSourceSpec _source;
};

/// Reads VTK XML binary blocks from raw or base64-encoded segments.
class XmlBinaryBlockPayloadReader final : public ScalarPayloadReader
{
public:
    explicit XmlBinaryBlockPayloadReader(PayloadSourceSpec source);

    void ReadDataInto(FileHandle& file,
                      const ScalarPayloadRequest& request,
                      std::byte* target,
                      size_t targetByteCount) const override;

private:
    PayloadSourceSpec _source;
};

/// Create a typed scalar VtValue from native-endian bytes.
///
/// The returned value holds a `VtArray<T>` matching `scalarType`. Aligned byte
/// storage is foreign-wrapped to avoid copies; unaligned storage is copied into
/// an aligned array and reported through `TF_DEBUG`.
PXR_NS::VtValue MakeTypedScalarValue(PXR_NS::VtUCharArray bytes, ScalarType scalarType, const std::string& debugContext);

} // namespace cae::vtk
