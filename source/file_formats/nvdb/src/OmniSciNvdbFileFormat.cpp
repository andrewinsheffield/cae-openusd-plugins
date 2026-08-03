// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciNvdbFileFormat.h"

#include "DynamicFileFormatArguments.h"

#include <omniSciFileFormatArgs/tokens.h>

#include <array>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciNvdbFileFormatTokens, OMNI_SCI_NVDB_FILE_FORMAT_TOKENS);

namespace
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 1> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciNvdbFileFormatTokens->ArgCacheMode },
        } };

    return DynamicFileFormatArgs;
}

PythonFileFormatConfig MakeConfig()
{
    PythonFileFormatConfig config;
    config.pluginName = "omniSciNvdbFileFormat";
    config.formatId = OmniSciNvdbFileFormatTokens->Id.GetText();
    config.version = OmniSciNvdbFileFormatTokens->Version.GetText();
    config.target = OmniSciNvdbFileFormatTokens->Target.GetText();
    config.extension = OmniSciNvdbFileFormatTokens->Extension.GetText();
    config.defaultModule = OmniSciNvdbFileFormatTokens->DefaultModule.GetText();
    return config;
}

} // namespace

OmniSciNvdbFileFormat::OmniSciNvdbFileFormat() : PythonFileFormatBase(MakeConfig())
{
}

OmniSciNvdbFileFormat::~OmniSciNvdbFileFormat() = default;

void OmniSciNvdbFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                const PcpDynamicFileFormatContext& context,
                                                                FileFormatArguments* args,
                                                                VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, GetDynamicFileFormatArgs(), args);
}

bool OmniSciNvdbFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, GetDynamicFileFormatArgs());
}

PXR_NAMESPACE_CLOSE_SCOPE
