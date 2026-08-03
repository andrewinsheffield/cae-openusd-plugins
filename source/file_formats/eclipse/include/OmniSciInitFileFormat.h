// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

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

/// Read-only USD file format for Eclipse INIT static reservoir properties.
///
/// INIT files do not describe grid geometry by themselves.  This reader
/// authors an overlay layer at the default case prim path and attaches stored
/// cell-sized properties through OmniSciFieldAPI, OmniSciArrayAPI, and
/// OmniSciReservoirCellPropertyAPI.  It intentionally does not apply
/// OmniSciReservoirCornerPointGridAPI or synthesize active-to-logical maps.
class OmniSciInitFileFormat : public SdfFileFormat, public PcpDynamicFileFormatInterface
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

    OmniSciInitFileFormat();
    ~OmniSciInitFileFormat() override;
};

// clang-format off
#define OMNI_SCI_INIT_FILE_FORMAT_TOKENS                  \
    ((Id,                      "OmniSciInitFileFormat")) \
    ((Version,                 "1.0"))                \
    ((Target,                  "usd"))                \
    ((Extension,               "init"))               \
    ((ArgCacheMode,            "cacheMode"))          \
    ((ArgReservoirKeywordMode, "reservoirKeywordMode"))

TF_DECLARE_PUBLIC_TOKENS(OmniSciInitFileFormatTokens, OMNI_SCI_INIT_FILE_FORMAT_TOKENS);
// clang-format on

TF_DECLARE_WEAK_AND_REF_PTRS(OmniSciInitFileFormat);

PXR_NAMESPACE_CLOSE_SCOPE
