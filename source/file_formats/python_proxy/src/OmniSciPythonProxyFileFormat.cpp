// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciPythonProxyFileFormat.h"

#include "DynamicFileFormatArguments.h"

#include <omniSciFileFormatArgs/tokens.h>

#include <array>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciPythonProxyFileFormatTokens, OMNI_SCI_PYTHON_PROXY_FILE_FORMAT_TOKENS);

namespace
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 6> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciPythonProxyFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatPythonModule, OmniSciPythonProxyFileFormatTokens->ArgPythonModule },
            { OmniSciFileFormatArgsTokens->omniCaeFormatPythonPath, OmniSciPythonProxyFileFormatTokens->ArgPythonPath },
            { OmniSciFileFormatArgsTokens->omniCaeFormatPythonReadFunction,
              OmniSciPythonProxyFileFormatTokens->ArgReadFunction },
            { OmniSciFileFormatArgsTokens->omniCaeFormatPythonCanReadFunction,
              OmniSciPythonProxyFileFormatTokens->ArgCanReadFunction },
            { OmniSciFileFormatArgsTokens->omniCaeFormatPythonLoadArrayFunction,
              OmniSciPythonProxyFileFormatTokens->ArgLoadArrayFunction },
        } };

    return DynamicFileFormatArgs;
}

/// Builds the PythonFileFormatConfig for OmniSciPythonProxyFileFormat from the
/// compile-time token values.  `defaultModule` is intentionally left empty:
/// the proxy format has no bundled reader and requires the caller to supply
/// `pythonModule` as a format argument at open time.
PythonFileFormatConfig MakeConfig()
{
    PythonFileFormatConfig config;
    config.pluginName = "omniSciPythonProxyFileFormat";
    config.formatId = OmniSciPythonProxyFileFormatTokens->Id.GetText();
    config.version = OmniSciPythonProxyFileFormatTokens->Version.GetText();
    config.target = OmniSciPythonProxyFileFormatTokens->Target.GetText();
    config.extension = OmniSciPythonProxyFileFormatTokens->Extension.GetText();
    config.defaultModule = OmniSciPythonProxyFileFormatTokens->DefaultModule.GetText();
    config.deriveMountPathFromIdentifier = false;
    return config;
}

} // namespace

OmniSciPythonProxyFileFormat::OmniSciPythonProxyFileFormat() : PythonFileFormatBase(MakeConfig())
{
}

OmniSciPythonProxyFileFormat::~OmniSciPythonProxyFileFormat() = default;

void OmniSciPythonProxyFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                       const PcpDynamicFileFormatContext& context,
                                                                       FileFormatArguments* args,
                                                                       VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, GetDynamicFileFormatArgs(), args);
}

bool OmniSciPythonProxyFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, GetDynamicFileFormatArgs());
}

PXR_NAMESPACE_CLOSE_SCOPE
