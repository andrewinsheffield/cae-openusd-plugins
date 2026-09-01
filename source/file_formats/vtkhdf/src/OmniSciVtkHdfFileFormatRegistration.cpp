// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DisablePXRWarnings.h"
#include "OmniSciVtkHdfFileFormat.h"
#include "debugCodes.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/instantiateType.h>
#include <pxr/usd/sdf/fileFormat.h>
CAE_DISABLE_PXR_WARNINGS_END

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciVtkHdfFileFormatTokens, OMNI_SCI_VTKHDF_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(CAE_VTKHDF_FILEFORMAT, "VTKHDF file-format diagnostics");
}

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(OmniSciVtkHdfFileFormat, SdfFileFormat);
}

PXR_NAMESPACE_CLOSE_SCOPE
