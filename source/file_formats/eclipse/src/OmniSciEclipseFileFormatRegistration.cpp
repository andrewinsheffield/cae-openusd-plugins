// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciEgridFileFormat.h"
#include "OmniSciGrdeclFileFormat.h"
#include "OmniSciInitFileFormat.h"
#include "OmniSciUnrstFileFormat.h"
#include "debugCodes.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(CAE_ECLIPSE_FILEFORMAT, "Eclipse reservoir file-format diagnostics");
}

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(OmniSciGrdeclFileFormat, SdfFileFormat);
    SDF_DEFINE_FILE_FORMAT(OmniSciEgridFileFormat, SdfFileFormat);
    SDF_DEFINE_FILE_FORMAT(OmniSciInitFileFormat, SdfFileFormat);
    SDF_DEFINE_FILE_FORMAT(OmniSciUnrstFileFormat, SdfFileFormat);
}

PXR_NAMESPACE_CLOSE_SCOPE
