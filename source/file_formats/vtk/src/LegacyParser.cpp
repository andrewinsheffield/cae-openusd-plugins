// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DebugCodes.h"
#include "DisablePXRWarnings.h"
#include "FileFormatError.h"
#include "Parser.h"
#include "ParserUtils.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace cae::vtk
{

namespace
{

std::string DecodeLegacyString(const std::string& encoded)
{
    std::string out;
    out.reserve(encoded.size());
    size_t i = 0;
    while (i < encoded.size())
    {
        if (encoded[i] == '%' && i + 2 < encoded.size())
        {
            const std::string hex = encoded.substr(i + 1, 2);
            char* end = nullptr;
            const long value = std::strtol(hex.c_str(), &end, 16);
            if (end == hex.c_str() + 2)
            {
                out.push_back(static_cast<char>(value));
                i += 3;
                continue;
            }
        }
        out.push_back(encoded[i]);
        ++i;
    }
    return out;
}

std::string ReadToken(std::istream& input)
{
    std::string token;
    char ch = '\0';
    while (input.get(ch))
    {
        if (!std::isspace(static_cast<unsigned char>(ch)))
        {
            token.push_back(ch);
            break;
        }
    }
    while (input.get(ch))
    {
        if (std::isspace(static_cast<unsigned char>(ch)))
            break;
        token.push_back(ch);
    }
    return token;
}

uint64_t CurrentOffset(std::istream& input)
{
    std::streampos pos = input.tellg();
    if (pos == std::streampos(-1) && input.eof())
    {
        input.clear();
        input.seekg(0, std::ios::end);
        pos = input.tellg();
    }
    if (pos == std::streampos(-1))
        throw cae::FileFormatError("Failed to query VTK file offset while parsing metadata");
    return static_cast<uint64_t>(pos);
}

uint64_t ConsumeBinaryPreamble(std::istream& input)
{
    while (input.peek() == '\r' || input.peek() == '\n')
        input.get();
    return CurrentOffset(input);
}

void SeekAbsolute(std::istream& input, uint64_t offset)
{
    if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw std::overflow_error("VTK file offset exceeds streamoff");
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input)
        throw cae::FileFormatError("Failed to seek while parsing VTK metadata");
}

uint64_t CheckedOffset(uint64_t offset, size_t byteCount)
{
    if (byteCount > std::numeric_limits<uint64_t>::max() - offset)
        throw std::overflow_error("VTK metadata byte offset overflow");
    return offset + byteCount;
}

void SkipOptionalMetadata(std::istream& input)
{
    const std::streampos pos = input.tellg();
    const std::string maybe = ReadToken(input);
    if (ToLower(maybe) != "metadata")
    {
        input.clear();
        input.seekg(pos);
        return;
    }

    std::string line;
    std::getline(input, line);
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
    }
}

template <typename T>
void ByteSwapInPlace(T* value)
{
    auto* bytes = reinterpret_cast<unsigned char*>(value);
    std::reverse(bytes, bytes + sizeof(T));
}

void SkipBinaryLegacyString(std::istream& input)
{
    const auto firstByte = static_cast<uint8_t>(input.peek());
    const uint8_t headerType = firstByte >> 6;
    size_t length = 0;
    if (headerType == 3)
    {
        uint8_t encoded = 0;
        input.read(reinterpret_cast<char*>(&encoded), 1);
        encoded <<= 2;
        encoded >>= 2;
        length = encoded;
    }
    else if (headerType == 2)
    {
        uint16_t encoded = 0;
        input.read(reinterpret_cast<char*>(&encoded), 2);
        ByteSwapInPlace(&encoded);
        encoded <<= 2;
        encoded >>= 2;
        length = encoded;
    }
    else if (headerType == 1)
    {
        uint32_t encoded = 0;
        input.read(reinterpret_cast<char*>(&encoded), 4);
        ByteSwapInPlace(&encoded);
        encoded <<= 2;
        encoded >>= 2;
        length = encoded;
    }
    else
    {
        uint64_t encoded = 0;
        input.read(reinterpret_cast<char*>(&encoded), 8);
        ByteSwapInPlace(&encoded);
        length = encoded;
    }

    if (!input)
        throw cae::FileFormatError("Failed to read binary legacy VTK string header");
    input.seekg(static_cast<std::streamoff>(length), std::ios::cur);
    if (!input)
        throw cae::FileFormatError("Failed to skip binary legacy VTK string payload");
}

std::string DebugContext(const std::string& filePath, const std::string& name, uint64_t offset)
{
    return filePath + " array='" + name + "' offset=" + std::to_string(offset);
}

Association CurrentAssociationOrNone(Association association)
{
    return association == Association::Field ? Association::None : association;
}

struct ParsedScalarPayload
{
    bool supported = false;
    ScalarType scalarType = ScalarType::Float32;
    std::string sourceScalarToken;
    ScalarPayloadSpec payload;
    std::string reason;
    uint64_t offset = 0;
};

void SkipUnsupportedLegacyPayload(std::istream& input, const std::string& scalarToken, size_t valueCount, bool binary)
{
    const std::string lower = ToLower(scalarToken);
    if (lower == "bit")
    {
        if (binary)
        {
            const uint64_t offset = ConsumeBinaryPreamble(input);
            const size_t byteCount = (valueCount + 7) / 8;
            SeekAbsolute(input, CheckedOffset(offset, byteCount));
        }
        else
        {
            for (size_t i = 0; i < valueCount; ++i)
                (void)ReadToken(input);
        }
        SkipOptionalMetadata(input);
        return;
    }

    if (lower == "string" || lower == "utf8_string")
    {
        if (binary)
        {
            (void)ConsumeBinaryPreamble(input);
            for (size_t i = 0; i < valueCount; ++i)
                SkipBinaryLegacyString(input);
        }
        else
        {
            std::string line;
            for (size_t i = 0; i < valueCount; ++i)
                std::getline(input, line);
        }
        SkipOptionalMetadata(input);
        return;
    }

    throw cae::FileFormatError("Cannot skip unsupported legacy VTK scalar token '" + scalarToken + "'");
}

ParsedScalarPayload ParseLegacyScalarPayload(std::istream& input,
                                             const std::string& filePath,
                                             const std::string& scalarToken,
                                             size_t tupleCount,
                                             int componentCount,
                                             bool binary)
{
    const size_t valueCount = CheckedMul(tupleCount, static_cast<size_t>(std::max(componentCount, 0)));
    ScalarTypeNormalization normalized = NormalizeLegacyScalarTypeToken(scalarToken);

    ParsedScalarPayload parsed;
    parsed.supported = normalized.supported;
    parsed.sourceScalarToken = scalarToken;
    parsed.reason = normalized.reason;
    if (!normalized.supported)
    {
        SkipUnsupportedLegacyPayload(input, scalarToken, valueCount, binary);
        return parsed;
    }

    if (binary && normalized.requiresStorageWidthResolution)
    {
        throw cae::FileFormatError("Binary legacy VTK scalar token '" + scalarToken +
                                   "' has platform-dependent width and cannot be read safely");
    }

    parsed.scalarType = normalized.scalarType;
    parsed.payload.source.storageKind = binary ? StorageKind::PlainBinary : StorageKind::Ascii;
    parsed.payload.request.scalarType = normalized.scalarType;
    parsed.payload.request.valueCount = valueCount;

    if (binary)
    {
        parsed.offset = ConsumeBinaryPreamble(input);
        const size_t byteCount = DecodedByteCount(parsed.payload.request);
        parsed.payload.source.segments.push_back({ parsed.offset, byteCount });
        SeekAbsolute(input, CheckedOffset(parsed.offset, byteCount));
    }
    else
    {
        parsed.offset = CurrentOffset(input);
        for (size_t i = 0; i < valueCount; ++i)
            (void)ReadToken(input);
        const uint64_t endOffset = CurrentOffset(input);
        if (endOffset < parsed.offset)
            throw cae::FileFormatError("Invalid legacy VTK ASCII payload byte range: " + filePath);
        parsed.payload.source.segments.push_back({ parsed.offset, endOffset - parsed.offset });
    }

    SkipOptionalMetadata(input);
    return parsed;
}

void AddArrayFromPayload(DatasetSpec* dataset,
                         ArrayNameRegistry* names,
                         const std::string& sourceName,
                         const std::string& fallbackName,
                         Association association,
                         ArrayRole role,
                         size_t tupleCount,
                         int componentCount,
                         ParsedScalarPayload parsed,
                         const std::string& filePath)
{
    const std::string debug = DebugContext(filePath, sourceName.empty() ? fallbackName : sourceName, parsed.offset);
    if (!parsed.supported)
    {
        AddSkippedArray(dataset, sourceName, association, role, parsed.sourceScalarToken, tupleCount, componentCount,
                        parsed.reason, debug);
        return;
    }

    if (!SupportsArrayShape(parsed.scalarType, componentCount))
    {
        AddSkippedArray(dataset, sourceName, association, role, parsed.sourceScalarToken, tupleCount, componentCount,
                        "unsupported component count or non-floating vector array", debug);
        return;
    }

    ArraySpec array;
    array.sourceName = sourceName;
    array.arrayName =
        role == ArrayRole::Generic ? names->MakeUniqueName(sourceName, fallbackName) : PXR_NS::TfToken(fallbackName);
    array.scalarType = parsed.scalarType;
    array.tupleCount = tupleCount;
    array.componentCount = componentCount;
    array.association = association;
    array.role = role;
    array.evaluationKind = EvaluationKindFor(parsed.scalarType, componentCount);
    array.payload = std::move(parsed.payload);
    array.sourceScalarToken = parsed.sourceScalarToken;
    array.debugContext = debug;
    dataset->arrays.push_back(std::move(array));
}

void AddLegacyPackedCellArray(DatasetSpec* dataset,
                              const std::string& filePath,
                              const std::string& packedName,
                              size_t cellCount,
                              size_t legacyValueCount,
                              bool binary,
                              std::istream& input)
{
    ScalarPayloadSpec payload;
    payload.source.storageKind = binary ? StorageKind::PlainBinary : StorageKind::Ascii;
    payload.request.scalarType = ScalarType::Int32;
    payload.request.valueCount = legacyValueCount;

    uint64_t offset = 0;
    if (binary)
    {
        offset = ConsumeBinaryPreamble(input);
        const size_t byteCount = CheckedMul(legacyValueCount, sizeof(int32_t));
        payload.source.segments.push_back({ offset, byteCount });
        SeekAbsolute(input, CheckedOffset(offset, byteCount));
    }
    else
    {
        offset = CurrentOffset(input);
        for (size_t i = 0; i < legacyValueCount; ++i)
            (void)ReadToken(input);
        const uint64_t endOffset = CurrentOffset(input);
        if (endOffset < offset)
            throw cae::FileFormatError("Invalid legacy VTK ASCII packed-cell payload byte range: " + filePath);
        payload.source.segments.push_back({ offset, endOffset - offset });
    }
    SkipOptionalMetadata(input);

    if (legacyValueCount < cellCount)
        throw cae::FileFormatError("Malformed legacy VTK packed-cell header: fewer values than cells");

    ArraySpec array;
    array.sourceName = packedName;
    array.arrayName = PXR_NS::TfToken(packedName);
    array.scalarType = ScalarType::Int32;
    array.tupleCount = legacyValueCount;
    array.componentCount = 1;
    array.role = ArrayRole::Connectivity;
    array.evaluationKind = ArrayEvaluationKind::DirectScalarPayload;
    array.payload = std::move(payload);
    array.sourceScalarToken = "int";
    array.debugContext = DebugContext(filePath, packedName, offset);

    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] recorded legacy packed cell array='%s' cells=%zu values=%zu debug='%s'\n", packedName.c_str(),
             cellCount, legacyValueCount, array.debugContext.c_str());
    dataset->arrays.push_back(std::move(array));
}

size_t ParseLegacyCellArray(DatasetSpec* dataset,
                            const std::string& filePath,
                            const std::string& offsetsName,
                            const std::string& connectivityName,
                            const std::string& packedName,
                            int versionMajor,
                            bool binary,
                            std::istream& input,
                            ArrayNameRegistry* names)
{
    if (versionMajor >= 5)
    {
        const auto offsetsSize = static_cast<size_t>(std::stoll(ReadToken(input)));
        const auto connectivitySize = static_cast<size_t>(std::stoll(ReadToken(input)));
        const size_t cellCount = offsetsSize > 0 ? offsetsSize - 1 : 0;

        if (ToLower(ReadToken(input)) != "offsets")
            throw cae::FileFormatError("Expected OFFSETS block in legacy VTK cell array");
        const std::string offsetsType = ReadToken(input);
        AddArrayFromPayload(dataset, names, offsetsName, offsetsName, Association::None, ArrayRole::Offsets, offsetsSize,
                            1, ParseLegacyScalarPayload(input, filePath, offsetsType, offsetsSize, 1, binary), filePath);

        if (ToLower(ReadToken(input)) != "connectivity")
            throw cae::FileFormatError("Expected CONNECTIVITY block in legacy VTK cell array");
        const std::string connectivityType = ReadToken(input);
        AddArrayFromPayload(dataset, names, connectivityName, connectivityName, Association::None,
                            ArrayRole::Connectivity, connectivitySize, 1,
                            ParseLegacyScalarPayload(input, filePath, connectivityType, connectivitySize, 1, binary),
                            filePath);
        return cellCount;
    }

    const auto cellCount = static_cast<size_t>(std::stoll(ReadToken(input)));
    const auto legacyValueCount = static_cast<size_t>(std::stoll(ReadToken(input)));
    AddLegacyPackedCellArray(dataset, filePath, packedName, cellCount, legacyValueCount, binary, input);
    return cellCount;
}

void ParseExtentsFromDimensions(DatasetSpec* dataset, int nx, int ny, int nz)
{
    dataset->minExtent = { 0, 0, 0 };
    dataset->maxExtent = { nx - 1, ny - 1, nz - 1 };
    dataset->pointCount = static_cast<size_t>(std::max(nx, 0) * std::max(ny, 0) * std::max(nz, 0));
    dataset->cellCount = static_cast<size_t>(std::max(nx - 1, 0) * std::max(ny - 1, 0) * std::max(nz - 1, 0));
}

void ParseExtents(DatasetSpec* dataset, int x0, int x1, int y0, int y1, int z0, int z1)
{
    dataset->minExtent = { x0, y0, z0 };
    dataset->maxExtent = { x1, y1, z1 };
    dataset->pointCount =
        static_cast<size_t>(std::max(x1 - x0 + 1, 0) * std::max(y1 - y0 + 1, 0) * std::max(z1 - z0 + 1, 0));
    dataset->cellCount = static_cast<size_t>(std::max(x1 - x0, 0) * std::max(y1 - y0, 0) * std::max(z1 - z0, 0));
}

DatasetKind ParseLegacyDatasetKind(const std::string& datasetType)
{
    if (datasetType == "polydata")
        return DatasetKind::PolyData;
    if (datasetType == "structured_points")
        return DatasetKind::StructuredPoints;
    if (datasetType == "structured_grid")
        return DatasetKind::StructuredGrid;
    if (datasetType == "rectilinear_grid")
        return DatasetKind::RectilinearGrid;
    if (datasetType == "unstructured_grid")
        return DatasetKind::UnstructuredGrid;
    throw cae::FileFormatError("Unsupported legacy VTK DATASET type: " + datasetType);
}

class LegacyTokenParser
{
public:
    LegacyTokenParser(DatasetSpec* dataset,
                      ArrayNameRegistry* names,
                      std::istream* input,
                      std::string filePath,
                      int versionMajor,
                      bool binary)
        : _dataset(dataset),
          _names(names),
          _input(input),
          _filePath(std::move(filePath)),
          _versionMajor(versionMajor),
          _binary(binary)
    {
    }

    void Parse()
    {
        while (true)
        {
            const std::streampos tokenPosition = _input->tellg();
            const std::string rawToken = ReadToken(*_input);
            if (rawToken.empty())
                return;
            if (const std::string token = ToLower(rawToken);
                ParseCommonToken(token, tokenPosition) || ParseDatasetToken(token) || ParseAttributeToken(token))
                continue;

            TF_WARN("Stopping legacy VTK metadata parse at unrecognized token '%s' in '%s'", rawToken.c_str(),
                    _filePath.c_str());
            return;
        }
    }

private:
    void AddPayload(const std::string& sourceName,
                    const std::string& fallbackName,
                    Association association,
                    ArrayRole role,
                    size_t tupleCount,
                    int componentCount,
                    const std::string& scalarToken)
    {
        AddArrayFromPayload(
            _dataset, _names, sourceName, fallbackName, association, role, tupleCount, componentCount,
            ParseLegacyScalarPayload(*_input, _filePath, scalarToken, tupleCount, componentCount, _binary), _filePath);
    }

    bool ParseCommonToken(std::string_view token, std::streampos tokenPosition)
    {
        if (token == "metadata")
        {
            _input->clear();
            _input->seekg(tokenPosition);
            SkipOptionalMetadata(*_input);
            return true;
        }
        if (token == "field")
        {
            ParseFieldData();
            return true;
        }
        if (token != "point_data" && token != "cell_data")
            return false;

        _association = token == "point_data" ? Association::Point : Association::Cell;
        _tupleCount = static_cast<size_t>(std::stoll(ReadToken(*_input)));
        return true;
    }

    bool ParseDatasetToken(const std::string& token)
    {
        switch (_dataset->kind)
        {
        case DatasetKind::PolyData:
            return ParsePolyDataToken(token);
        case DatasetKind::StructuredPoints:
            return ParseStructuredPointsToken(token);
        case DatasetKind::StructuredGrid:
            return ParseStructuredGridToken(token);
        case DatasetKind::RectilinearGrid:
            return ParseRectilinearGridToken(token);
        case DatasetKind::UnstructuredGrid:
            return ParseUnstructuredGridToken(token);
        case DatasetKind::ImageData:
            return false;
        }
        return false;
    }

    bool ParsePolyDataToken(const std::string& token)
    {
        if (token == "points")
        {
            ParsePoints();
            return true;
        }
        struct CellArrayNames
        {
            const char* token;
            const char* offsets;
            const char* connectivity;
            const char* packed;
        };
        constexpr std::array<CellArrayNames, 4> names = { {
            { "vertices", "vertsConnectivityOffsets", "vertsConnectivityArray", "vertsPackedConnectivityArray" },
            { "lines", "linesConnectivityOffsets", "linesConnectivityArray", "linesPackedConnectivityArray" },
            { "polygons", "polysConnectivityOffsets", "polysConnectivityArray", "polysPackedConnectivityArray" },
            { "triangle_strips", "stripsConnectivityOffsets", "stripsConnectivityArray", "stripsPackedConnectivityArray" },
        } };
        const auto found =
            std::find_if(names.begin(), names.end(), [&](const CellArrayNames& item) { return token == item.token; });
        if (found == names.end())
            return false;
        _dataset->cellCount += ParseLegacyCellArray(_dataset, _filePath, found->offsets, found->connectivity,
                                                    found->packed, _versionMajor, _binary, *_input, _names);
        return true;
    }

    void ParseDimensions()
    {
        const int nx = std::stoi(ReadToken(*_input));
        const int ny = std::stoi(ReadToken(*_input));
        const int nz = std::stoi(ReadToken(*_input));
        ParseExtentsFromDimensions(_dataset, nx, ny, nz);
    }

    void ParseExtent()
    {
        ParseExtents(_dataset, std::stoi(ReadToken(*_input)), std::stoi(ReadToken(*_input)),
                     std::stoi(ReadToken(*_input)), std::stoi(ReadToken(*_input)), std::stoi(ReadToken(*_input)),
                     std::stoi(ReadToken(*_input)));
    }

    bool ParseStructuredPointsToken(std::string_view token)
    {
        if (token == "dimensions")
            ParseDimensions();
        else if (token == "extent")
            ParseExtent();
        else if (token == "origin")
            _dataset->origin = { std::stod(ReadToken(*_input)), std::stod(ReadToken(*_input)),
                                 std::stod(ReadToken(*_input)) };
        else if (token == "spacing" || token == "aspect_ratio")
            _dataset->spacing = { std::stod(ReadToken(*_input)), std::stod(ReadToken(*_input)),
                                  std::stod(ReadToken(*_input)) };
        else
            return false;
        return true;
    }

    bool ParseStructuredGridToken(std::string_view token)
    {
        if (token == "dimensions")
            ParseDimensions();
        else if (token == "extent")
            ParseExtent();
        else if (token == "points")
            ParsePoints();
        else
            return false;
        return true;
    }

    bool ParseRectilinearGridToken(std::string_view token)
    {
        if (token == "dimensions")
            ParseDimensions();
        else if (token == "extent")
            ParseExtent();
        else if (token == "x_coordinates")
            ParseCoordinate("xCoordinates", ArrayRole::XCoordinates);
        else if (token == "y_coordinates")
            ParseCoordinate("yCoordinates", ArrayRole::YCoordinates);
        else if (token == "z_coordinates")
            ParseCoordinate("zCoordinates", ArrayRole::ZCoordinates);
        else
            return false;
        return true;
    }

    bool ParseUnstructuredGridToken(std::string_view token)
    {
        if (token == "points")
            ParsePoints();
        else if (token == "cells")
            _dataset->cellCount =
                ParseLegacyCellArray(_dataset, _filePath, "connectivityOffsets", "connectivityArray",
                                     "connectivityPackedArray", _versionMajor, _binary, *_input, _names);
        else if (token == "cell_types")
        {
            const auto count = static_cast<size_t>(std::stoll(ReadToken(*_input)));
            _dataset->cellCount = count;
            AddPayload("cellTypes", "cellTypes", Association::None, ArrayRole::CellTypes, count, 1, "int");
        }
        else
            return false;
        return true;
    }

    void ParsePoints()
    {
        const auto count = static_cast<size_t>(std::stoll(ReadToken(*_input)));
        _dataset->pointCount = count;
        AddPayload("points", "points", Association::None, ArrayRole::Points, count, 3, ReadToken(*_input));
    }

    void ParseCoordinate(const char* name, ArrayRole role)
    {
        const auto count = static_cast<size_t>(std::stoll(ReadToken(*_input)));
        AddPayload(name, name, Association::None, role, count, 1, ReadToken(*_input));
    }

    bool ParseAttributeToken(std::string_view token)
    {
        if (token == "scalars")
            ParseScalarField();
        else if (token == "vectors" || token == "normals")
            ParseFixedComponentField(3);
        else if (token == "tensors6")
            ParseFixedComponentField(6);
        else if (token == "tensors")
            ParseFixedComponentField(9);
        else if (token == "texture_coordinates")
            ParseTextureCoordinates();
        else if (token == "global_ids" || token == "pedigree_ids" || token == "edge_flags")
            ParseFixedComponentField(1);
        else if (token == "color_scalars")
            ParseColorScalars();
        else if (token == "lookup_table")
            ParseLookupTable();
        else
            return false;
        return true;
    }

    void ParseScalarField()
    {
        const std::string sourceName = DecodeLegacyString(ReadToken(*_input));
        const std::string scalarToken = ReadToken(*_input);
        std::string next = ReadToken(*_input);
        int components = 1;
        if (ToLower(next) != "lookup_table")
        {
            components = std::stoi(next);
            next = ReadToken(*_input);
        }
        if (ToLower(next) != "lookup_table")
            throw cae::FileFormatError("Expected LOOKUP_TABLE in legacy VTK SCALARS block");
        (void)ReadToken(*_input);
        AddPayload(sourceName, "array", CurrentAssociationOrNone(_association), ArrayRole::Generic, _tupleCount,
                   components, scalarToken);
    }

    void ParseFixedComponentField(int components)
    {
        const std::string sourceName = DecodeLegacyString(ReadToken(*_input));
        AddPayload(sourceName, "array", CurrentAssociationOrNone(_association), ArrayRole::Generic, _tupleCount,
                   components, ReadToken(*_input));
    }

    void ParseTextureCoordinates()
    {
        const std::string sourceName = DecodeLegacyString(ReadToken(*_input));
        const int components = std::stoi(ReadToken(*_input));
        AddPayload(sourceName, "array", CurrentAssociationOrNone(_association), ArrayRole::Generic, _tupleCount,
                   components, ReadToken(*_input));
    }

    void ParseColorScalars()
    {
        const std::string sourceName = DecodeLegacyString(ReadToken(*_input));
        const int components = std::stoi(ReadToken(*_input));
        AddPayload(sourceName, "array", CurrentAssociationOrNone(_association), ArrayRole::Generic, _tupleCount,
                   components, _binary ? "unsigned_char" : "float");
    }

    void ParseFieldData()
    {
        (void)DecodeLegacyString(ReadToken(*_input));
        const int arrayCount = std::stoi(ReadToken(*_input));
        for (int index = 0; index < arrayCount; ++index)
        {
            const std::string arrayNameToken = ReadToken(*_input);
            if (arrayNameToken == "NULL_ARRAY")
                continue;
            const std::string sourceName = DecodeLegacyString(arrayNameToken);
            const int components = std::stoi(ReadToken(*_input));
            const auto tuples = static_cast<size_t>(std::stoll(ReadToken(*_input)));
            AddPayload(sourceName, "array", CurrentAssociationOrNone(_association), ArrayRole::Generic, tuples,
                       components, ReadToken(*_input));
        }
    }

    void ParseLookupTable()
    {
        (void)ReadToken(*_input);
        const auto size = static_cast<size_t>(std::stoll(ReadToken(*_input)));
        (void)ParseLegacyScalarPayload(*_input, _filePath, _binary ? "unsigned_char" : "float", size, 4, _binary);
    }

    DatasetSpec* _dataset;
    ArrayNameRegistry* _names;
    std::istream* _input;
    std::string _filePath;
    int _versionMajor;
    bool _binary;
    Association _association = Association::None;
    size_t _tupleCount = 0;
};

} // namespace

bool LegacyParser::CanParse(const std::string& filePath) const
{
    const std::string prefix = ReadFilePrefix(filePath);
    return StartsWith(prefix, "# vtk DataFile") || StartsWith(prefix, "# vtk");
}

DatasetSpec LegacyParser::Parse(const std::string& filePath, const ReadOptions& /*options*/) const
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open legacy VTK file: " + filePath);

    DatasetSpec dataset;
    dataset.file.filePath = filePath;
    dataset.file.byteOrder = ByteOrder::BigEndian;
    dataset.file.debugContext = filePath;

    std::string line;
    if (!std::getline(input, line) || line.rfind("# vtk DataFile Version ", 0) != 0)
        throw cae::FileFormatError("Not a legacy VTK file: " + filePath);

    const std::string versionText = line.substr(std::strlen("# vtk DataFile Version "));
    const size_t dot = versionText.find('.');
    const int versionMajor = std::stoi(versionText.substr(0, dot));
    if (std::getline(input, dataset.sourceDescription) && !dataset.sourceDescription.empty() &&
        dataset.sourceDescription.back() == '\r')
        dataset.sourceDescription.pop_back();
    if (!std::getline(input, line))
        throw cae::FileFormatError("Malformed legacy VTK file: missing ASCII/BINARY line");
    const bool binary = ToLower(PXR_NS::TfStringTrim(line)) == "binary";

    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] parse legacy metadata file='%s' version=%s mode=%s title='%s'\n", filePath.c_str(),
             versionText.c_str(), binary ? "BINARY" : "ASCII", dataset.sourceDescription.c_str());

    if (ToLower(ReadToken(input)) != "dataset")
        throw cae::FileFormatError("Only DATASET-style legacy VTK files are supported: " + filePath);

    const std::string datasetType = ToLower(ReadToken(input));
    dataset.kind = ParseLegacyDatasetKind(datasetType);
    ArrayNameRegistry names;
    LegacyTokenParser(&dataset, &names, &input, filePath, versionMajor, binary).Parse();

    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] parsed legacy summary file='%s' arrays=%zu skipped=%zu points=%zu cells=%zu\n", filePath.c_str(),
             dataset.arrays.size(), dataset.skippedArrays.size(), dataset.pointCount, dataset.cellCount);
    return dataset;
}

} // namespace cae::vtk
