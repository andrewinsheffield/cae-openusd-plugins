// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file FileFormatCore.h
///
/// Source-private orchestration API for the single public OmniSciVtkFileFormat.

#include "CaeFileFormatData.h"
#include "DatasetSpec.h"
#include "DatasetSpecCache.h"
#include "DisablePXRWarnings.h"
#include "Types.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/pxr.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <string>

namespace cae::vtk
{

struct FileFormatOptions
{
    PXR_NS::SdfPath rootPath;
    PXR_NS::CaeFileFormatData::CacheMode cacheMode = PXR_NS::CaeFileFormatData::CacheMode::All;
    ReadOptions readOptions;
};

struct ReadResult
{
    ReadResult() = default;
    ReadResult(const ReadResult&) = delete;
    ReadResult& operator=(const ReadResult&) = delete;
    ReadResult(ReadResult&&) noexcept = default;
    ReadResult& operator=(ReadResult&&) noexcept = default;

    PXR_NS::SdfLayerRefPtr structureLayer;
    PXR_NS::CaeFileFormatDataRefPtr fileData;
};

/// Parse flat USD file-format arguments into typed VTK options.
///
/// This resolves the authored root path, lazy-array cache mode, and `ioThreads`
/// hint. Options that do not affect metadata parsing are intentionally kept out
/// of the parsed-spec cache identity.
FileFormatOptions ParseFileFormatOptions(const std::string& resolvedPath,
                                         const PXR_NS::SdfLayer::FileFormatArguments& args);

/// Main implementation entry point used by OmniSciVtkFileFormat::Read.
///
ReadResult ReadVtk(const std::string& resolvedPath, const PXR_NS::SdfLayer::FileFormatArguments& args);

} // namespace cae::vtk
