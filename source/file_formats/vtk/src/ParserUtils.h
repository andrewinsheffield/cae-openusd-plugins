// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file ParserUtils.h
///
/// Small parser-side helpers shared by legacy VTK and VTK XML metadata
/// parsers. These helpers normalize names and scalar type tokens without
/// reading heavy array payloads.

#include "ContainerUtils.h"
#include "DatasetSpec.h"
#include "DisablePXRWarnings.h"
#include "Types.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cae::vtk
{

/// Read a small prefix of `filePath` for content sniffing.
std::string ReadFilePrefix(const std::string& filePath);

/// Return a lowercase copy of `value` using ASCII casing rules.
std::string ToLower(std::string value);

/// Return whether `text` starts with `prefix`.
bool StartsWith(std::string_view text, std::string_view prefix);

/// Return whether `text` contains `needle`.
bool Contains(std::string_view text, std::string_view needle);

/// Multiply two metadata counts with overflow checking.
size_t CheckedMul(size_t lhs, size_t rhs);

/// Add a byte count to a file offset with overflow checking.
uint64_t CheckedOffset(uint64_t offset, size_t byteCount);

/// Seek a metadata input stream to an absolute file offset.
void SeekAbsolute(std::istream& input, uint64_t offset);

/// Result of normalizing a file-format scalar type token.
struct ScalarTypeNormalization
{
    /// True when the token maps to one of the numeric scalar types supported
    /// by the reader.
    bool supported = false;

    /// Normalized scalar type. Meaningful only when `supported` is true.
    ScalarType scalarType = ScalarType::Float32;

    /// Original token from the file, preserved for diagnostics.
    std::string sourceToken;

    /// Reason the token is unsupported, or extra context for ambiguous tokens.
    std::string reason;

    /// True for legacy tokens whose binary storage width may depend on the
    /// writer platform. The parser must resolve or reject those before making
    /// a direct binary `ArraySpec`.
    bool requiresStorageWidthResolution = false;
};

/// Normalize a legacy VTK scalar token such as `float`, `unsigned_int`, or
/// `vtkidtype` into the supported numeric scalar set.
ScalarTypeNormalization NormalizeLegacyScalarTypeToken(const std::string& token);

/// Normalize a VTK XML scalar token such as `Float32`, `UInt8`, or `Int64`
/// into the supported numeric scalar set.
ScalarTypeNormalization NormalizeXmlScalarTypeToken(const std::string& token);

/// Return true when `componentCount` can be exposed for `scalarType`.
bool SupportsArrayShape(ScalarType scalarType, int componentCount);

/// Return the lazy evaluation strategy for a supported array shape.
ArrayEvaluationKind EvaluationKindFor(ScalarType scalarType, int componentCount);

/// Record a parsed-but-unexposed array with useful diagnostic context.
void AddSkippedArray(DatasetSpec* dataset,
                     std::string_view sourceName,
                     Association association,
                     ArrayRole role,
                     std::string_view sourceScalarToken,
                     size_t tupleCount,
                     int componentCount,
                     std::string reason,
                     std::string debugContext);

/// Tracks deterministic USD-facing array names within one authored scope.
///
/// VTK permits duplicate and non-identifier names. This helper produces valid
/// identifiers, supplies a fallback for unnamed structural arrays, and
/// auto-renames duplicates by appending a numeric suffix.
class ArrayNameRegistry
{
public:
    /// Return a valid unique token for `sourceName`.
    ///
    /// `fallbackBase` is used when `sourceName` is empty. Both inputs are
    /// sanitized with `TfMakeValidIdentifier` before duplicate resolution.
    PXR_NS::TfToken MakeUniqueName(const std::string& sourceName, const std::string& fallbackBase);

    /// Forget all names recorded for the current scope.
    void Clear();

private:
    cae::StringUnorderedSet _usedNames;
    cae::StringUnorderedMap<size_t> _nextSuffixByBaseName;
};

} // namespace cae::vtk
