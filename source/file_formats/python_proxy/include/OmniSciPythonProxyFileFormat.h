// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/// @file OmniSciPythonProxyFileFormat.h
///
/// USD SdfFileFormat plugin for the `.pydf` file extension.
///
/// `OmniSciPythonProxyFileFormat` is a thin shell around `PythonFileFormatBase` that
/// provides a concrete, registered format for rapid prototyping and testing
/// of Python-backed CAE readers without writing any additional C++ code.
///
/// Because it has no hardcoded Python module, callers **must** supply the
/// `pythonModule` format argument (and optionally `pythonPath`) when opening
/// a layer:
///
/// ```python
/// identifier = Sdf.Layer.CreateIdentifier(
///     "my_data.pydf",
///     {"pythonModule": "my_reader",
///      "pythonPath": "/path/to/my/reader"},
/// )
/// stage = Usd.Stage.Open(identifier)
/// ```
///
/// See `PythonFileFormatBase.h` for the full Python callback contract and the
/// lazy-field manifest format.

#pragma once

#include "DisablePXRWarnings.h"
#include "PythonFileFormatBase.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>
CAE_DISABLE_PXR_WARNINGS_END

PXR_NAMESPACE_OPEN_SCOPE

/**
 * @class OmniSciPythonProxyFileFormat
 *
 * Concrete SdfFileFormat for `.pydf` files.  All I/O is delegated to a
 * Python module supplied by the caller via format arguments.
 *
 * This format serves two purposes:
 *  1. **Escape hatch** -- lets Python developers iterate on a file format
 *     reader without a C++ build cycle.
 *  2. **Reference implementation** -- the tests in
 *     `tests/python/file_format_python_proxy/` demonstrate the expected
 *     Python callback shapes and lazy-array patterns.
 *
 * @see PythonFileFormatBase
 */
class OmniSciPythonProxyFileFormat : public PythonFileFormatBase, public PcpDynamicFileFormatInterface
{
public:
    void ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                             const PcpDynamicFileFormatContext& context,
                                             FileFormatArguments* args,
                                             VtValue* contextDependencyData) const override;

    bool CanAttributeDefaultValueChangeAffectFileFormatArguments(const TfToken& attributeName,
                                                                 const VtValue& oldValue,
                                                                 const VtValue& newValue,
                                                                 const VtValue& contextDependencyData) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

    OmniSciPythonProxyFileFormat();
    ~OmniSciPythonProxyFileFormat() override;
};

/// Public tokens for the OmniSciPythonProxyFileFormat plugin.
///
/// These match the values registered in plugInfo.json and are exposed so
/// that callers can reference argument keys by token rather than raw strings.
///
/// | Token                | Value                        |
/// |----------------------|------------------------------|
/// | Id                   | "OmniSciPythonProxyFileFormat"  |
/// | Version              | "1.0"                        |
/// | Target               | "usd"                        |
/// | Extension            | "pydf"                       |
/// | DefaultModule        | ""  (caller must supply)     |
/// | ArgCacheMode         | "cacheMode"                  |
/// | ArgPythonModule      | "pythonModule"               |
/// | ArgPythonPath        | "pythonPath"                 |
/// | ArgReadFunction      | "pythonReadFunction"         |
/// | ArgCanReadFunction   | "pythonCanReadFunction"      |
/// | ArgLoadArrayFunction | "pythonLoadArrayFunction"    |
// clang-format off
#define OMNI_SCI_PYTHON_PROXY_FILE_FORMAT_TOKENS              \
    ((Id,                   "OmniSciPythonProxyFileFormat")) \
    ((Version,              "1.0"))                       \
    ((Target,               "usd"))                       \
    ((Extension,            "pydf"))                      \
    ((DefaultModule,        ""))                          \
    ((ArgCacheMode,         "cacheMode"))                 \
    /* Python callback arguments */                       \
    ((ArgPythonModule,      "pythonModule"))              \
    ((ArgPythonPath,        "pythonPath"))                \
    ((ArgReadFunction,      "pythonReadFunction"))        \
    ((ArgCanReadFunction,   "pythonCanReadFunction"))     \
    ((ArgLoadArrayFunction, "pythonLoadArrayFunction"))

TF_DECLARE_PUBLIC_TOKENS(OmniSciPythonProxyFileFormatTokens, OMNI_SCI_PYTHON_PROXY_FILE_FORMAT_TOKENS);
// clang-format on

TF_DECLARE_WEAK_AND_REF_PTRS(OmniSciPythonProxyFileFormat);

PXR_NAMESPACE_CLOSE_SCOPE
