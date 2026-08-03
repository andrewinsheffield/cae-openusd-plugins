// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciEnSightFileFormat.h"
#include "debugCodes.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(CAE_ENSIGHT_FILEFORMAT, "EnSight file-format diagnostics");
}

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(OmniSciEnSightFileFormat, SdfFileFormat);
}

PXR_NAMESPACE_CLOSE_SCOPE
