// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciNpzFileFormat.h"

#include "DynamicFileFormatArguments.h"

#include <omniSciFileFormatArgs/tokens.h>

#include <array>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciNpzFileFormatTokens, OMNI_SCI_NPZ_FILE_FORMAT_TOKENS);

namespace
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 7> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciNpzFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatNpzSchema, OmniSciNpzFileFormatTokens->ArgSchema },
            { OmniSciFileFormatArgsTokens->omniCaeFormatNpzCoordsArray, OmniSciNpzFileFormatTokens->ArgCoordsArray },
            { OmniSciFileFormatArgsTokens->omniCaeFormatNpzCoordsArrayX, OmniSciNpzFileFormatTokens->ArgCoordsArrayX },
            { OmniSciFileFormatArgsTokens->omniCaeFormatNpzCoordsArrayY, OmniSciNpzFileFormatTokens->ArgCoordsArrayY },
            { OmniSciFileFormatArgsTokens->omniCaeFormatNpzCoordsArrayZ, OmniSciNpzFileFormatTokens->ArgCoordsArrayZ },
            { OmniSciFileFormatArgsTokens->omniCaeFormatNpzAllowPickle, OmniSciNpzFileFormatTokens->ArgAllowPickle },
        } };

    return DynamicFileFormatArgs;
}

PythonFileFormatConfig MakeConfig()
{
    PythonFileFormatConfig config;
    config.pluginName = "omniSciNumpyFileFormat";
    config.formatId = OmniSciNpzFileFormatTokens->Id.GetText();
    config.version = OmniSciNpzFileFormatTokens->Version.GetText();
    config.target = OmniSciNpzFileFormatTokens->Target.GetText();
    config.extension = OmniSciNpzFileFormatTokens->Extension.GetText();
    config.defaultModule = OmniSciNpzFileFormatTokens->DefaultModule.GetText();
    return config;
}

} // namespace

OmniSciNpzFileFormat::OmniSciNpzFileFormat() : PythonFileFormatBase(MakeConfig())
{
}

OmniSciNpzFileFormat::~OmniSciNpzFileFormat() = default;

void OmniSciNpzFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                               const PcpDynamicFileFormatContext& context,
                                                               FileFormatArguments* args,
                                                               VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, GetDynamicFileFormatArgs(), args);
}

bool OmniSciNpzFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, GetDynamicFileFormatArgs());
}

PXR_NAMESPACE_CLOSE_SCOPE
