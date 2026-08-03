// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DebugCodes.h"
#include "DisablePXRWarnings.h"
#include "OmniSciVtkFileFormat.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/usd/sdf/fileFormat.h>
CAE_DISABLE_PXR_WARNINGS_END

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(CAE_VTK_FILEFORMAT, "VTK file-format diagnostics");
}

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(OmniSciVtkFileFormat, SdfFileFormat);
}

PXR_NAMESPACE_CLOSE_SCOPE
