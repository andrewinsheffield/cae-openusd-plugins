// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file DatasetSpec.h
///
/// Immutable metadata produced by the parser pass. Specs describe what the file
/// contains and how heavy arrays can be loaded later, but they do not own live
/// FileHandle instances or decoded heavy buffers.

#include "DisablePXRWarnings.h"
#include "Types.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cae::vtk
{

enum class DatasetKind
{
    ImageData,
    StructuredPoints,
    StructuredGrid,
    RectilinearGrid,
    PolyData,
    UnstructuredGrid,
};

enum class Association
{
    None,
    Point,
    Cell,
    Field,
};

enum class ArrayRole
{
    Generic,
    Points,
    XCoordinates,
    YCoordinates,
    ZCoordinates,
    Connectivity,
    Offsets,
    CellTypes,
};

enum class ArrayEvaluationKind
{
    DirectScalarPayload,
    ReinterpretedVector,
    PrependZero,
};

enum class StorageKind
{
    Ascii,
    PlainBinary,
    XmlBinary,
    XmlBase64Binary,
};

/// File-level metadata needed to reconstruct a runtime file context.
struct FileSpec
{
    std::string filePath;
    ByteOrder byteOrder = ByteOrder::LittleEndian;
    XmlHeaderType xmlHeaderType = XmlHeaderType::UInt32;
    XmlCompressor xmlCompressor = XmlCompressor::None;
    std::string debugContext;
};

/// Absolute source file byte range for one payload segment.
struct PayloadSegmentSpec
{
    uint64_t startOffset = 0;
    uint64_t byteCount = 0;
};

/// Lightweight description of where one scalar payload's source bytes live.
struct PayloadSourceSpec
{
    StorageKind storageKind = StorageKind::PlainBinary;
    std::vector<PayloadSegmentSpec> segments;
};

/// Describes how to read a direct scalar payload.
struct ScalarPayloadSpec
{
    PayloadSourceSpec source;
    ScalarPayloadRequest request;
};

/// Metadata for one USD-facing array.
///
/// `ArraySpec` is produced by the parser and consumed by the file-format /
/// lazy-array layer. It describes the final USD-facing value shape as well as
/// the stored scalar payload needed to build that value. It must remain
/// metadata-only: no live file handles, decoded appended buffers, or heavy
/// array values should be stored here.
struct ArraySpec
{
    /// Name as it appeared in the VTK file.
    ///
    /// May be empty for structural arrays that do not have a source name, such
    /// as legacy points or derived topology arrays. This is preserved for
    /// diagnostics and for authoring `OmniSciFieldAPI:name` when it differs
    /// from `arrayName`.
    std::string sourceName;

    /// Normalized USD-facing array/attribute name.
    ///
    /// The parser/file-format layer should make this name deterministic and
    /// unique within the authored scope. Duplicate VTK names should be
    /// auto-renamed before they reach lazy array registration.
    PXR_NS::TfToken arrayName;

    /// Normalized scalar type of the payload and final scalar components.
    ///
    /// This is the VTK scalar type after parser normalization. For example,
    /// legacy `vtkidtype` should become `Int64`; ambiguous legacy `long`
    /// handling should be resolved or rejected by the parser before this spec
    /// is exposed for authoring.
    ScalarType scalarType = ScalarType::Float32;

    /// Number of logical tuples in the USD-facing array.
    ///
    /// For scalar arrays this equals the value count. For vector arrays this is
    /// the number of vectors, not the number of scalar components.
    size_t tupleCount = 0;

    /// Number of scalar components per tuple in the source/logical array.
    ///
    /// `1` means scalar. Floating `2`, `3`, and `4` component arrays may be
    /// exposed as `GfVec*` arrays through `ReinterpretedVector`. Unsupported
    /// component counts should remain parseable when possible, then be skipped
    /// by USD authoring with useful diagnostics.
    int componentCount = 1;

    /// VTK association for the array.
    ///
    /// Point and cell associations map to point/cell fields. `Field` or `None`
    /// are used for field-data style arrays that are not associated with mesh
    /// elements.
    Association association = Association::None;

    /// Semantic role used by dataset/topology authoring.
    ///
    /// Generic arrays are authored as fields. Structural roles such as
    /// `Points`, `Connectivity`, `Offsets`, and coordinate roles may be routed
    /// to schema/topology attributes instead.
    ArrayRole role = ArrayRole::Generic;

    /// Lazy transformation needed to produce the USD-facing value.
    ///
    /// Most arrays are direct scalar payloads. Some arrays reinterpret scalar
    /// payloads as vectors or prepend the zero offset implied by source topology
    /// formats that store only cell-end offsets.
    ArrayEvaluationKind evaluationKind = ArrayEvaluationKind::DirectScalarPayload;

    /// Stored scalar payload used by this array evaluation.
    ///
    /// For `DirectScalarPayload`, `ReinterpretedVector`, and `PrependZero`,
    /// this describes the concrete payload to read.
    ScalarPayloadSpec payload;

    /// Original scalar token, for diagnostics. Useful for legacy `long`,
    /// `unsigned_long`, and `vtkidtype` normalization.
    std::string sourceScalarToken;

    /// Human-readable file/array context for diagnostics.
    ///
    /// Should include enough context to identify the original file location or
    /// XML element when a lazy read fails later.
    std::string debugContext;
};

/// Metadata for an array that was parsed but will not be exposed as a value.
///
/// The parser records these when it can understand enough metadata to explain
/// the skip without reading heavy data. The file-format layer should report
/// these with enough context to answer "why is this array missing?".
struct SkippedArraySpec
{
    /// Name as it appeared in the VTK file, if present.
    std::string sourceName;

    /// VTK association reported for the skipped array.
    Association association = Association::None;

    /// Semantic role, when the parser can determine one.
    ArrayRole role = ArrayRole::Generic;

    /// Scalar token as it appeared in the file, if relevant.
    std::string sourceScalarToken;

    /// Tuple/component metadata from the file, when known.
    size_t tupleCount = 0;
    int componentCount = 1;

    /// Human-readable reason this array is not exposed.
    std::string reason;

    /// File/array context for diagnostics.
    std::string debugContext;
};

/// Metadata for one parsed VTK dataset.
struct DatasetSpec
{
    FileSpec file;
    DatasetKind kind = DatasetKind::PolyData;
    std::string sourceDescription;

    std::array<int, 3> minExtent = { 0, 0, 0 };
    std::array<int, 3> maxExtent = { 0, 0, 0 };
    std::array<double, 3> origin = { 0.0, 0.0, 0.0 };
    std::array<double, 3> spacing = { 1.0, 1.0, 1.0 };

    size_t pointCount = 0;
    size_t cellCount = 0;

    std::vector<ArraySpec> arrays;
    std::vector<SkippedArraySpec> skippedArrays;
};

} // namespace cae::vtk
