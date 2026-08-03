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

/**
 * @class OmniSciCgnsFileFormat
 *
 * USD file format plugin for reading CGNS files. Heavy array data is not
 * loaded eagerly; instead, the layer uses a CaeFileFormatData backend so that
 * individual fields are fetched on demand.
 *
 * This is a read-only file format: WriteToString / WriteToStream are not
 * supported.
 */
class OmniSciCgnsFileFormat : public SdfFileFormat, public PcpDynamicFileFormatInterface
{
public:
    bool CanRead(const std::string& filePath) const override;
    bool Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const override;

    // Read-only: write operations are not supported.
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

    OmniSciCgnsFileFormat();
    ~OmniSciCgnsFileFormat() override;
};

// clang-format off
#define OMNI_SCI_CGNS_FILE_FORMAT_TOKENS                    \
    ((Id,             "OmniSciCgnsFileFormat"))            \
    ((Version,        "1.0"))                           \
    ((Target,         "usd"))                           \
    ((Extension,      "cgns"))                          \
    /* Common arguments */                              \
    ((ArgCacheMode,   "cacheMode"))                     \
    /* Time arguments */                                \
    ((ArgTimeScale,   "timeScale"))                     \
    ((ArgTimeOffset,  "timeOffset"))                    \
    ((ArgTimeSource,  "timeSource"))                    \
    /* Filter arguments */                              \
    ((ArgBaseName,    "baseName"))                      \
    ((ArgZoneName,    "zoneName"))                      \
    /* Precision arguments */                           \
    ((ArgIntSize,     "intSize"))                       \
    ((ArgFloatSize,   "floatSize"))                     \
    /* Prim type tokens */                              \
    ((CGNSBase,          "CGNSBase"))                   \
    ((CGNSZone,          "CGNSZone"))                   \
    ((CGNSFlowSolution,  "CGNSFlowSolution"))           \
    ((CGNSGridCoordinates, "CGNSGridCoordinates"))

TF_DECLARE_PUBLIC_TOKENS(OmniSciCgnsFileFormatTokens, OMNI_SCI_CGNS_FILE_FORMAT_TOKENS);
// clang-format on

TF_DECLARE_WEAK_AND_REF_PTRS(OmniSciCgnsFileFormat);

PXR_NAMESPACE_CLOSE_SCOPE
