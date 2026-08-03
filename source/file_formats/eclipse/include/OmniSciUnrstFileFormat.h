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

/// Read-only USD file format for Eclipse UNRST restart reservoir properties.
///
/// UNRST files are result overlays: they provide stored cell-sized solution
/// keywords grouped by restart report step.  The reader exposes selected
/// keywords as time-sampled lazy arrays on the default case prim path.  It does
/// not compose the matching EGRID/INIT files, derive missing fields, or build
/// active-to-logical maps.
class OmniSciUnrstFileFormat : public SdfFileFormat, public PcpDynamicFileFormatInterface
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

    OmniSciUnrstFileFormat();
    ~OmniSciUnrstFileFormat() override;
};

// clang-format off
#define OMNI_SCI_UNRST_FILE_FORMAT_TOKENS                   \
    ((Id,                      "OmniSciUnrstFileFormat"))  \
    ((Version,                 "1.0"))                     \
    ((Target,                  "usd"))                     \
    ((Extension,               "unrst"))                   \
    ((ArgCacheMode,            "cacheMode"))               \
    ((ArgReservoirKeywordMode, "reservoirKeywordMode"))    \
    ((ArgTimeScale,            "timeScale"))               \
    ((ArgTimeOffset,           "timeOffset"))              \
    ((ArgTimeSource,           "timeSource"))

TF_DECLARE_PUBLIC_TOKENS(OmniSciUnrstFileFormatTokens, OMNI_SCI_UNRST_FILE_FORMAT_TOKENS);
// clang-format on

TF_DECLARE_WEAK_AND_REF_PTRS(OmniSciUnrstFileFormat);

PXR_NAMESPACE_CLOSE_SCOPE
