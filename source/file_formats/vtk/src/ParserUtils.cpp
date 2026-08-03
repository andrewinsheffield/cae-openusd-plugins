// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ParserUtils.h"

#include "DisablePXRWarnings.h"
#include "FileFormatError.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/stringUtils.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <cctype>
#include <fstream>
#include <istream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cae::vtk
{

namespace
{

constexpr size_t SniffByteCount = 4096;

ScalarTypeNormalization Supported(std::string_view token, ScalarType scalarType)
{
    ScalarTypeNormalization out;
    out.supported = true;
    out.scalarType = scalarType;
    out.sourceToken = token;
    return out;
}

ScalarTypeNormalization Unsupported(std::string_view token, std::string reason)
{
    ScalarTypeNormalization out;
    out.supported = false;
    out.sourceToken = token;
    out.reason = std::move(reason);
    return out;
}

} // namespace

std::string ReadFilePrefix(const std::string& filePath)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        return {};

    std::string prefix(SniffByteCount, '\0');
    input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    prefix.resize(static_cast<size_t>(input.gcount()));
    return prefix;
}

std::string ToLower(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool Contains(std::string_view text, std::string_view needle)
{
    return cae::StringContains(text, needle);
}

size_t CheckedMul(size_t lhs, size_t rhs)
{
    if (rhs != 0 && lhs > std::numeric_limits<size_t>::max() / rhs)
        throw std::overflow_error("VTK metadata count overflow");
    return lhs * rhs;
}

uint64_t CheckedOffset(uint64_t offset, size_t byteCount)
{
    if (byteCount > std::numeric_limits<uint64_t>::max() - offset)
        throw std::overflow_error("VTK metadata byte offset overflow");
    return offset + byteCount;
}

void SeekAbsolute(std::istream& input, uint64_t offset)
{
    if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw std::overflow_error("VTK file offset exceeds streamoff");
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input)
        throw cae::FileFormatError("Failed to seek while parsing VTK metadata");
}

ScalarTypeNormalization NormalizeLegacyScalarTypeToken(const std::string& token)
{
    const std::string lower = ToLower(token);
    if (lower == "char" || lower == "signed_char")
        return Supported(token, ScalarType::Int8);
    if (lower == "unsigned_char")
        return Supported(token, ScalarType::UInt8);
    if (lower == "short")
        return Supported(token, ScalarType::Int16);
    if (lower == "unsigned_short")
        return Supported(token, ScalarType::UInt16);
    if (lower == "int")
        return Supported(token, ScalarType::Int32);
    if (lower == "unsigned_int")
        return Supported(token, ScalarType::UInt32);
    if (lower == "vtkidtype" || lower == "vtktypeint64" || lower == "long_long")
        return Supported(token, ScalarType::Int64);
    if (lower == "vtktypeuint64" || lower == "unsigned_long_long")
        return Supported(token, ScalarType::UInt64);
    if (lower == "float")
        return Supported(token, ScalarType::Float32);
    if (lower == "double")
        return Supported(token, ScalarType::Float64);

    if (lower == "long" || lower == "unsigned_long")
    {
        ScalarTypeNormalization out = Supported(token, lower == "long" ? ScalarType::Int64 : ScalarType::UInt64);
        out.requiresStorageWidthResolution = true;
        out.reason = "legacy long storage width may depend on the writer platform";
        return out;
    }

    if (lower == "bit")
        return Unsupported(token, "legacy bit arrays are not handled by the numeric scalar reader");
    if (lower == "string" || lower == "utf8_string")
        return Unsupported(token, "string arrays are outside the numeric reader scope");

    return Unsupported(token, "unknown legacy VTK scalar type token");
}

ScalarTypeNormalization NormalizeXmlScalarTypeToken(const std::string& token)
{
    const std::string lower = ToLower(token);
    if (lower == "int8")
        return Supported(token, ScalarType::Int8);
    if (lower == "uint8")
        return Supported(token, ScalarType::UInt8);
    if (lower == "int16")
        return Supported(token, ScalarType::Int16);
    if (lower == "uint16")
        return Supported(token, ScalarType::UInt16);
    if (lower == "int32")
        return Supported(token, ScalarType::Int32);
    if (lower == "uint32")
        return Supported(token, ScalarType::UInt32);
    if (lower == "int64")
        return Supported(token, ScalarType::Int64);
    if (lower == "uint64")
        return Supported(token, ScalarType::UInt64);
    if (lower == "float32")
        return Supported(token, ScalarType::Float32);
    if (lower == "float64")
        return Supported(token, ScalarType::Float64);

    if (lower == "string")
        return Unsupported(token, "string arrays are outside the numeric reader scope");

    return Unsupported(token, "unknown VTK XML scalar type token");
}

bool SupportsArrayShape(ScalarType scalarType, int componentCount)
{
    if (componentCount == 1)
        return true;
    if (componentCount >= 2 && componentCount <= 4)
        return scalarType == ScalarType::Float32 || scalarType == ScalarType::Float64;
    return false;
}

ArrayEvaluationKind EvaluationKindFor(ScalarType scalarType, int componentCount)
{
    if (componentCount == 1)
        return ArrayEvaluationKind::DirectScalarPayload;
    if (componentCount >= 2 && componentCount <= 4 &&
        (scalarType == ScalarType::Float32 || scalarType == ScalarType::Float64))
    {
        return ArrayEvaluationKind::ReinterpretedVector;
    }
    throw cae::FileFormatError("Unsupported VTK array component shape");
}

void AddSkippedArray(DatasetSpec* dataset,
                     std::string_view sourceName,
                     Association association,
                     ArrayRole role,
                     std::string_view sourceScalarToken,
                     size_t tupleCount,
                     int componentCount,
                     std::string reason,
                     std::string debugContext)
{
    SkippedArraySpec skipped;
    skipped.sourceName = sourceName;
    skipped.association = association;
    skipped.role = role;
    skipped.sourceScalarToken = sourceScalarToken;
    skipped.tupleCount = tupleCount;
    skipped.componentCount = componentCount;
    skipped.reason = std::move(reason);
    skipped.debugContext = std::move(debugContext);
    dataset->skippedArrays.push_back(std::move(skipped));
}

PXR_NS::TfToken ArrayNameRegistry::MakeUniqueName(const std::string& sourceName, const std::string& fallbackBase)
{
    const std::string rawBase = sourceName.empty() ? fallbackBase : sourceName;
    const std::string base = PXR_NS::TfMakeValidIdentifier(rawBase);

    auto nextSuffix = _nextSuffixByBaseName.find(base);
    if (nextSuffix == _nextSuffixByBaseName.end() && _usedNames.insert(base).second)
    {
        _nextSuffixByBaseName.emplace(base, 1);
        return PXR_NS::TfToken(base);
    }

    size_t suffix = _nextSuffixByBaseName[base];
    while (true)
    {
        const std::string candidate = base + "_" + std::to_string(suffix++);
        if (_usedNames.insert(candidate).second)
        {
            _nextSuffixByBaseName[base] = suffix;
            return PXR_NS::TfToken(candidate);
        }
    }
}

void ArrayNameRegistry::Clear()
{
    _usedNames.clear();
    _nextSuffixByBaseName.clear();
}

} // namespace cae::vtk
