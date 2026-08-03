// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file OmniSciVtkFileFormat.h
///
/// Public OpenUSD file-format class for the VTK reader. The implementation
/// lives in source-private files under `src/`.

#include "DisablePXRWarnings.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <iosfwd>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

/// Single public file-format plugin for legacy VTK and serial VTK XML files.
class OmniSciVtkFileFormat : public SdfFileFormat, public PcpDynamicFileFormatInterface
{
public:
    bool CanRead(const std::string& filePath) const override;
    bool Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const override;

    bool WriteToString(const SdfLayer& layer, std::string* str, const std::string& comment = std::string()) const override;
    bool WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const override;

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

    OmniSciVtkFileFormat();
    ~OmniSciVtkFileFormat() override;
};

// clang-format off
#define OMNI_SCI_VTK_FILE_FORMAT_TOKENS           \
    ((Id,           "OmniSciVtkFileFormat"))     \
    ((Version,      "1.0"))                       \
    ((Target,       "usd"))                       \
    ((Extension,    "vtk"))                       \
    ((ArgCacheMode, "cacheMode"))                 \
    ((ArgIoThreads, "ioThreads"))
// clang-format on

TF_DECLARE_PUBLIC_TOKENS(OmniSciVtkFileFormatTokens, OMNI_SCI_VTK_FILE_FORMAT_TOKENS);

TF_DECLARE_WEAK_AND_REF_PTRS(OmniSciVtkFileFormat);

PXR_NAMESPACE_CLOSE_SCOPE
