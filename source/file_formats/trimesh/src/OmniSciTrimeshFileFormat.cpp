// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciTrimeshFileFormat.h"

#include "DynamicFileFormatArguments.h"

#include <omniSciFileFormatArgs/tokens.h>

#include <array>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciTrimeshFileFormatTokens, OMNI_SCI_TRIMESH_FILE_FORMAT_TOKENS);

namespace
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 1> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciTrimeshFileFormatTokens->ArgCacheMode },
        } };

    return DynamicFileFormatArgs;
}

PythonFileFormatConfig MakeConfig()
{
    PythonFileFormatConfig config;
    config.pluginName = "omniSciTrimeshFileFormat";
    config.formatId = OmniSciTrimeshFileFormatTokens->Id.GetText();
    config.version = OmniSciTrimeshFileFormatTokens->Version.GetText();
    config.target = OmniSciTrimeshFileFormatTokens->Target.GetText();
    config.extension = OmniSciTrimeshFileFormatTokens->Extension.GetText();
    config.extensionAliases = "ply,3mf";
    config.defaultModule = OmniSciTrimeshFileFormatTokens->DefaultModule.GetText();
    return config;
}

} // namespace

OmniSciTrimeshFileFormat::OmniSciTrimeshFileFormat() : PythonFileFormatBase(MakeConfig())
{
}

OmniSciTrimeshFileFormat::~OmniSciTrimeshFileFormat() = default;

void OmniSciTrimeshFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                   const PcpDynamicFileFormatContext& context,
                                                                   FileFormatArguments* args,
                                                                   VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, GetDynamicFileFormatArgs(), args);
}

bool OmniSciTrimeshFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, GetDynamicFileFormatArgs());
}

PXR_NAMESPACE_CLOSE_SCOPE
