// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "FileFormatCore.h"

#include "ArrayEvaluator.h"
#include "DebugCodes.h"
#include "DisablePXRWarnings.h"
#include "FileFormatError.h"
#include "MountPath.h"
#include "Parser.h"

#include <omniSci/arrayAPI.h>
#include <omniSci/dataset.h>
#include <omniSci/fieldAPI.h>
#include <omniSci/tokens.h>
#include <omniSciVtk/imageDataAPI.h>
#include <omniSciVtk/polyDataAPI.h>
#include <omniSciVtk/rectilinearGridAPI.h>
#include <omniSciVtk/structuredGridAPI.h>
#include <omniSciVtk/unstructuredGridAPI.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/refPtr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace cae::vtk
{

namespace
{

const char* CacheModeName(PXR_NS::CaeFileFormatData::CacheMode mode)
{
    switch (mode)
    {
    case PXR_NS::CaeFileFormatData::CacheMode::All:
        return "all";
    case PXR_NS::CaeFileFormatData::CacheMode::Static:
        return "static";
    case PXR_NS::CaeFileFormatData::CacheMode::None:
        return "none";
    }
    return "unknown";
}

std::string GetArg(const PXR_NS::SdfLayer::FileFormatArguments& args, const char* key)
{
    const auto iter = args.find(key);
    return iter == args.end() ? std::string() : iter->second;
}

int ParsePositiveIntArg(const PXR_NS::SdfLayer::FileFormatArguments& args, const char* key, int fallback)
{
    const std::string value = GetArg(args, key);
    if (value.empty())
        return fallback;

    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end != value.c_str() + value.size() || parsed <= 0)
    {
        TF_WARN("VTK file-format argument '%s' expected a positive integer, got '%s'; using %d", key, value.c_str(),
                fallback);
        return fallback;
    }

    return static_cast<int>(std::min<long>(parsed, static_cast<long>(std::numeric_limits<int>::max())));
}

PXR_NS::TfToken AssociationToken(Association association)
{
    switch (association)
    {
    case Association::Point:
        return PXR_NS::OmniSciTokens->node;
    case Association::Cell:
        return PXR_NS::OmniSciTokens->element;
    case Association::None:
    case Association::Field:
        return PXR_NS::OmniSciTokens->none;
    }
    return PXR_NS::OmniSciTokens->none;
}

bool IsStructuralRole(ArrayRole role)
{
    return role != ArrayRole::Generic;
}

bool ShouldAuthorFieldMetadata(const ArraySpec& array)
{
    return !IsStructuralRole(array.role);
}

PXR_NS::TfToken MakeArrayValueAttrName(const PXR_NS::TfToken& arrayName)
{
    return PXR_NS::TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

PXR_NS::TfToken ScalarValueTypeName(const ArraySpec& array)
{
    switch (array.scalarType)
    {
    case ScalarType::Int8:
    case ScalarType::Int16:
    case ScalarType::Int32:
        return PXR_NS::TfToken("int[]");
    case ScalarType::UInt8:
        return PXR_NS::TfToken("uchar[]");
    case ScalarType::UInt16:
    case ScalarType::UInt32:
        return PXR_NS::TfToken("uint[]");
    case ScalarType::Int64:
        return PXR_NS::TfToken("int64[]");
    case ScalarType::UInt64:
        return PXR_NS::TfToken("uint64[]");
    case ScalarType::Float32:
        return PXR_NS::TfToken("float[]");
    case ScalarType::Float64:
        return PXR_NS::TfToken("double[]");
    }
    throw cae::FileFormatError("Unsupported VTK scalar value type for array: " + array.debugContext);
}

PXR_NS::TfToken VectorValueTypeName(const ArraySpec& array)
{
    if (array.scalarType != ScalarType::Float32 && array.scalarType != ScalarType::Float64)
        throw cae::FileFormatError("Unsupported non-floating VTK vector array: " + array.debugContext);
    if (array.componentCount < 2 || array.componentCount > 4)
        throw cae::FileFormatError("Unsupported VTK vector component count for array: " + array.debugContext);

    const char* prefix = array.scalarType == ScalarType::Float32 ? "float" : "double";
    return PXR_NS::TfToken(std::string(prefix) + std::to_string(array.componentCount) + "[]");
}

PXR_NS::TfToken ValueTypeName(const ArraySpec& array)
{
    switch (array.evaluationKind)
    {
    case ArrayEvaluationKind::DirectScalarPayload:
    case ArrayEvaluationKind::PrependZero:
        return ScalarValueTypeName(array);
    case ArrayEvaluationKind::ReinterpretedVector:
        return VectorValueTypeName(array);
    }
    throw cae::FileFormatError("Unsupported VTK array evaluation kind for value type: " + array.debugContext);
}

void ApplyDatasetKindApi(const DatasetSpec& spec, const PXR_NS::UsdPrim& prim)
{
    const PXR_NS::GfVec3i minExtent(spec.minExtent[0], spec.minExtent[1], spec.minExtent[2]);
    const PXR_NS::GfVec3i maxExtent(spec.maxExtent[0], spec.maxExtent[1], spec.maxExtent[2]);

    switch (spec.kind)
    {
    case DatasetKind::ImageData:
    case DatasetKind::StructuredPoints:
    {
        PXR_NS::OmniSciVtkImageDataAPI api = PXR_NS::OmniSciVtkImageDataAPI::Apply(prim);
        api.CreateOriginAttr().Set(PXR_NS::GfVec3f(
            static_cast<float>(spec.origin[0]), static_cast<float>(spec.origin[1]), static_cast<float>(spec.origin[2])));
        api.CreateSpacingAttr().Set(PXR_NS::GfVec3f(static_cast<float>(spec.spacing[0]),
                                                    static_cast<float>(spec.spacing[1]),
                                                    static_cast<float>(spec.spacing[2])));
        api.CreateMinExtentAttr().Set(minExtent);
        api.CreateMaxExtentAttr().Set(maxExtent);
        break;
    }
    case DatasetKind::StructuredGrid:
    {
        PXR_NS::OmniSciVtkStructuredGridAPI api = PXR_NS::OmniSciVtkStructuredGridAPI::Apply(prim);
        api.CreateMinExtentAttr().Set(minExtent);
        api.CreateMaxExtentAttr().Set(maxExtent);
        break;
    }
    case DatasetKind::RectilinearGrid:
    {
        PXR_NS::OmniSciVtkRectilinearGridAPI api = PXR_NS::OmniSciVtkRectilinearGridAPI::Apply(prim);
        api.CreateMinExtentAttr().Set(minExtent);
        api.CreateMaxExtentAttr().Set(maxExtent);
        break;
    }
    case DatasetKind::PolyData:
        PXR_NS::OmniSciVtkPolyDataAPI::Apply(prim);
        break;
    case DatasetKind::UnstructuredGrid:
        PXR_NS::OmniSciVtkUnstructuredGridAPI::Apply(prim);
        break;
    }
}

PXR_NS::SdfLayerRefPtr AuthorStructure(const DatasetSpec& spec, const FileFormatOptions& options)
{
    PXR_NS::SdfLayerRefPtr layer = PXR_NS::SdfLayer::CreateAnonymous();
    PXR_NS::UsdStageRefPtr stage = PXR_NS::UsdStage::Open(layer);
    if (!stage)
        throw cae::FileFormatError("Failed to create VTK structure stage");

    PXR_NS::UsdPrim datasetPrim = PXR_NS::OmniSciDataset::Define(stage, options.rootPath).GetPrim();
    if (!datasetPrim)
        throw cae::FileFormatError("Failed to author VTK dataset prim: " + options.rootPath.GetString());

    ApplyDatasetKindApi(spec, datasetPrim);

    for (const ArraySpec& array : spec.arrays)
    {
        PXR_NS::OmniSciArrayAPI arrayAPI = PXR_NS::OmniSciArrayAPI::Apply(datasetPrim, array.arrayName);
        arrayAPI.CreateDeviceAttr().Set(PXR_NS::TfToken("cpu"));

        if (ShouldAuthorFieldMetadata(array))
        {
            PXR_NS::OmniSciFieldAPI fieldAPI = PXR_NS::OmniSciFieldAPI::Apply(datasetPrim, array.arrayName);
            fieldAPI.CreateNameAttr().Set(array.sourceName.empty() ? array.arrayName.GetString() : array.sourceName);
            fieldAPI.CreateAssociationAttr().Set(AssociationToken(array.association));
        }

        TF_DEBUG(CAE_VTK_FILEFORMAT)
            .Msg("[VTK] authored array metadata name='%s' role=%d association=%d debug='%s'\n", array.arrayName.GetText(),
                 static_cast<int>(array.role), static_cast<int>(array.association), array.debugContext.c_str());
    }

    for (const SkippedArraySpec& skipped : spec.skippedArrays)
    {
        if (IsStructuralRole(skipped.role))
        {
            throw cae::FileFormatError("Required VTK structural array was skipped: " + skipped.sourceName +
                                       " reason: " + skipped.reason);
        }

        TF_WARN("Skipping unsupported VTK array '%s' association=%d components=%d tuples=%zu scalar='%s': %s%s%s",
                skipped.sourceName.c_str(), static_cast<int>(skipped.association), skipped.componentCount,
                skipped.tupleCount, skipped.sourceScalarToken.c_str(), skipped.reason.c_str(),
                skipped.debugContext.empty() ? "" : " context=", skipped.debugContext.c_str());
    }

    return layer;
}

PXR_NS::CaeFileFormatDataRefPtr RegisterFileData(const DatasetSpec& spec, const FileFormatOptions& options)
{
    PXR_NS::CaeFileFormatDataRefPtr fileData = PXR_NS::CreateCaeFileFormatData(options.cacheMode);

    auto file = std::make_shared<FileHandle>(spec.file, options.readOptions);
    const PXR_NS::SdfPath datasetPath = options.rootPath;
    for (const ArraySpec& array : spec.arrays)
    {
        const PXR_NS::TfToken valueType = ValueTypeName(array);
        PXR_NS::CaeFileFormatData::Loader loader = [file, array]()
        {
            ArrayEvaluator evaluator(*file);
            return evaluator.Evaluate(array);
        };

        TF_DEBUG(CAE_VTK_FILEFORMAT)
            .Msg("[VTK] register lazy array name='%s' type='%s' root='%s' debug='%s'\n", array.arrayName.GetText(),
                 valueType.GetText(), datasetPath.GetText(), array.debugContext.c_str());
        const PXR_NS::TfToken attrName = MakeArrayValueAttrName(array.arrayName);
        fileData->RegisterLazySingleState(datasetPath, attrName, valueType, 0.0, std::move(loader));
    }

    return fileData;
}

} // namespace

FileFormatOptions ParseFileFormatOptions(const std::string& resolvedPath,
                                         const PXR_NS::SdfLayer::FileFormatArguments& args)
{
    FileFormatOptions options;
    options.rootPath = PXR_NS::CaeResolveRootPrimPath(resolvedPath, args);
    options.cacheMode = PXR_NS::CaeFileFormatData::ParseCacheMode(args);
    options.readOptions.ioThreads = ParsePositiveIntArg(args, "ioThreads", options.readOptions.ioThreads);

    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] options root='%s' cacheMode='%s' ioThreads=%d\n", options.rootPath.GetText(),
             CacheModeName(options.cacheMode), options.readOptions.ioThreads);
    return options;
}

ReadResult ReadVtk(const std::string& resolvedPath, const PXR_NS::SdfLayer::FileFormatArguments& args)
{
    TF_DEBUG(CAE_VTK_FILEFORMAT).Msg("[VTK] ReadVtk path='%s' args=%zu\n", resolvedPath.c_str(), args.size());
    for (const auto& arg : args)
    {
        TF_DEBUG(CAE_VTK_FILEFORMAT).Msg("[VTK]   arg %s='%s'\n", arg.first.c_str(), arg.second.c_str());
    }

    const FileFormatOptions options = ParseFileFormatOptions(resolvedPath, args);
    std::unique_ptr<Parser> parser = CreateParserForFile(resolvedPath);
    if (!parser)
        throw cae::FileFormatError("Unsupported or unrecognized VTK file: " + resolvedPath);

    static DatasetSpecCache specCache;
    std::shared_ptr<const DatasetSpec> spec = specCache.GetOrParse(resolvedPath, options.readOptions, *parser);

    ReadResult result;
    result.structureLayer = AuthorStructure(*spec, options);
    result.fileData = RegisterFileData(*spec, options);
    return result;
}

} // namespace cae::vtk
