// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DisablePXRWarnings.h"
#include "PythonFileFormatBase.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>
CAE_DISABLE_PXR_WARNINGS_END

PXR_NAMESPACE_OPEN_SCOPE

class OmniSciTrimeshFileFormat : public PythonFileFormatBase, public PcpDynamicFileFormatInterface
{
public:
    OmniSciTrimeshFileFormat();
    ~OmniSciTrimeshFileFormat() override;

    void ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                             const PcpDynamicFileFormatContext& context,
                                             FileFormatArguments* args,
                                             VtValue* contextDependencyData) const override;

    bool CanAttributeDefaultValueChangeAffectFileFormatArguments(const TfToken& attributeName,
                                                                 const VtValue& oldValue,
                                                                 const VtValue& newValue,
                                                                 const VtValue& contextDependencyData) const override;
};

#define OMNI_SCI_TRIMESH_FILE_FORMAT_TOKENS                                                                            \
    ((Id, "OmniSciTrimeshFileFormat"))((Version, "1.0"))((Target, "usd"))((Extension, "stl"))(                         \
        (DefaultModule, "cae_trimesh"))((ArgCacheMode, "cacheMode"))

TF_DECLARE_PUBLIC_TOKENS(OmniSciTrimeshFileFormatTokens, OMNI_SCI_TRIMESH_FILE_FORMAT_TOKENS);

TF_DECLARE_WEAK_AND_REF_PTRS(OmniSciTrimeshFileFormat);

PXR_NAMESPACE_CLOSE_SCOPE
