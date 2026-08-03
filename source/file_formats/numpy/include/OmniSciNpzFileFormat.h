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

class OmniSciNpzFileFormat : public PythonFileFormatBase, public PcpDynamicFileFormatInterface
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

    OmniSciNpzFileFormat();
    ~OmniSciNpzFileFormat() override;
};

// clang-format off
#define OMNI_SCI_NPZ_FILE_FORMAT_TOKENS                      \
    ((Id,               "OmniSciNpzFileFormat"))            \
    ((Version,          "1.0"))                          \
    ((Target,           "usd"))                          \
    ((Extension,        "npz"))                          \
    ((DefaultModule,    "cae_npz"))                      \
    ((ArgCacheMode,     "cacheMode"))                    \
    /* Schema arguments */                               \
    ((ArgSchema,        "schema"))                       \
    ((ArgCoordsArray,   "coordsArray"))                  \
    ((ArgCoordsArrayX,  "coordsArrayX"))                 \
    ((ArgCoordsArrayY,  "coordsArrayY"))                 \
    ((ArgCoordsArrayZ,  "coordsArrayZ"))                 \
    ((ArgAllowPickle,   "allowPickle"))

TF_DECLARE_PUBLIC_TOKENS(OmniSciNpzFileFormatTokens, OMNI_SCI_NPZ_FILE_FORMAT_TOKENS);
// clang-format on

TF_DECLARE_WEAK_AND_REF_PTRS(OmniSciNpzFileFormat);

PXR_NAMESPACE_CLOSE_SCOPE
