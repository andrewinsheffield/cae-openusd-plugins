// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DisablePXRWarnings.h"
#include "PythonFileFormatBase.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>
CAE_DISABLE_PXR_WARNINGS_END

PXR_NAMESPACE_OPEN_SCOPE

class OmniSciNpyFileFormat : public PythonFileFormatBase, public PcpDynamicFileFormatInterface
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

    OmniSciNpyFileFormat();
    ~OmniSciNpyFileFormat() override;
};

// clang-format off
#define OMNI_SCI_NPY_FILE_FORMAT_TOKENS                      \
    ((Id,               "OmniSciNpyFileFormat"))            \
    ((Version,          "1.0"))                          \
    ((Target,           "usd"))                          \
    ((Extension,        "npy"))                          \
    ((DefaultModule,    "cae_npy"))                      \
    ((ArgCacheMode,     "cacheMode"))                    \
    /* Array arguments */                                \
    ((ArgArrayName,     "arrayName"))                    \
    ((ArgAllowPickle,   "allowPickle"))

TF_DECLARE_PUBLIC_TOKENS(OmniSciNpyFileFormatTokens, OMNI_SCI_NPY_FILE_FORMAT_TOKENS);
// clang-format on

TF_DECLARE_WEAK_AND_REF_PTRS(OmniSciNpyFileFormat);

PXR_NAMESPACE_CLOSE_SCOPE
