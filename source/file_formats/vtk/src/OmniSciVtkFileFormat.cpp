// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciVtkFileFormat.h"

#include "DebugCodes.h"
#include "DisablePXRWarnings.h"
#include "DynamicFileFormatArguments.h"
#include "FileFormatCore.h"
#include "MountPath.h"
#include "Parser.h"
#include "ResolverAsset.h"

#include <omniSciFileFormatArgs/tokens.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/sdf/abstractData.h>
#include <pxr/usd/sdf/layer.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <array>
#include <exception>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciVtkFileFormatTokens, OMNI_SCI_VTK_FILE_FORMAT_TOKENS);

namespace
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 2> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciVtkFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatStreamingIoThreads, OmniSciVtkFileFormatTokens->ArgIoThreads },
        } };

    return DynamicFileFormatArgs;
}

} // namespace

OmniSciVtkFileFormat::OmniSciVtkFileFormat()
    : SdfFileFormat(OmniSciVtkFileFormatTokens->Id,
                    OmniSciVtkFileFormatTokens->Version,
                    OmniSciVtkFileFormatTokens->Target,
                    OmniSciVtkFileFormatTokens->Extension)
{
}

OmniSciVtkFileFormat::~OmniSciVtkFileFormat() = default;

bool OmniSciVtkFileFormat::CanRead(const std::string& filePath) const
{
    try
    {
        const CaeResolverAssetPtr asset = CaeResolveAsset(filePath);
        const auto result = static_cast<bool>(cae::vtk::CreateParserForFile(asset->LocalPath()));
        TF_DEBUG(CAE_VTK_FILEFORMAT)
            .Msg("[VTK] OmniSciVtkFileFormat::CanRead('%s') -> %d\n", filePath.c_str(), result ? 1 : 0);
        return result;
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_DEBUG(CAE_VTK_FILEFORMAT)
            .Msg("[VTK] OmniSciVtkFileFormat::CanRead('%s') resolver failure: %s\n", filePath.c_str(), ex.what());
        return false;
    }
}

bool OmniSciVtkFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const
{
    if (!layer)
        return false;

    const SdfLayer::FileFormatArguments args = layer->GetFileFormatArguments();
    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] OmniSciVtkFileFormat::Read path='%s' metadataOnly=%d args=%zu\n", resolvedPath.c_str(),
             metadataOnly ? 1 : 0, args.size());

    try
    {
        const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
        const CaeResolverAssetPtr asset = CaeOpenResolverAsset(identifier, ArResolvedPath(resolvedPath));
        const SdfLayer::FileFormatArguments readArgs = CaePrepareResolverArguments(identifier, args);
        cae::vtk::ReadResult result = cae::vtk::ReadVtk(asset->LocalPath(), readArgs);
        if (!result.structureLayer || !result.fileData)
            return false;

        result.fileData->KeepAlive(asset);
        result.fileData->CopyFrom(_GetLayerData(*result.structureLayer));
        SdfAbstractDataRefPtr data = result.fileData;
        _SetLayerData(layer, data);

        const SdfPath rootPath = CaeResolveRootPrimPath(identifier, args);
        CaeAuthorMountPathOvers(layer, rootPath);
        return true;
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciVtkFileFormat: failed to read '%s': %s", resolvedPath.c_str(), ex.what());
        return false;
    }
}

void OmniSciVtkFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                               const PcpDynamicFileFormatContext& context,
                                                               FileFormatArguments* args,
                                                               VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, GetDynamicFileFormatArgs(), args);
}

bool OmniSciVtkFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, GetDynamicFileFormatArgs());
}

bool OmniSciVtkFileFormat::WriteToString(const SdfLayer&, std::string* str, const std::string&) const
{
    if (str)
        str->clear();
    return false;
}

bool OmniSciVtkFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
