// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciNpyFileFormat.h"

#include "DynamicFileFormatArguments.h"

#include <omniSciFileFormatArgs/tokens.h>

#include <array>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciNpyFileFormatTokens, OMNI_SCI_NPY_FILE_FORMAT_TOKENS);

namespace
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 3> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciNpyFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatNpyArrayName, OmniSciNpyFileFormatTokens->ArgArrayName },
            { OmniSciFileFormatArgsTokens->omniCaeFormatNpyAllowPickle, OmniSciNpyFileFormatTokens->ArgAllowPickle },
        } };

    return DynamicFileFormatArgs;
}

PythonFileFormatConfig MakeConfig()
{
    PythonFileFormatConfig config;
    config.pluginName = "omniSciNumpyFileFormat";
    config.formatId = OmniSciNpyFileFormatTokens->Id.GetText();
    config.version = OmniSciNpyFileFormatTokens->Version.GetText();
    config.target = OmniSciNpyFileFormatTokens->Target.GetText();
    config.extension = OmniSciNpyFileFormatTokens->Extension.GetText();
    config.defaultModule = OmniSciNpyFileFormatTokens->DefaultModule.GetText();
    return config;
}

} // namespace

OmniSciNpyFileFormat::OmniSciNpyFileFormat() : PythonFileFormatBase(MakeConfig())
{
}

OmniSciNpyFileFormat::~OmniSciNpyFileFormat() = default;

void OmniSciNpyFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                               const PcpDynamicFileFormatContext& context,
                                                               FileFormatArguments* args,
                                                               VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, GetDynamicFileFormatArgs(), args);
}

bool OmniSciNpyFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, GetDynamicFileFormatArgs());
}

PXR_NAMESPACE_CLOSE_SCOPE
