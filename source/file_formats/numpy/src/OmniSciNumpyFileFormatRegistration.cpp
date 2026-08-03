// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciNpyFileFormat.h"
#include "OmniSciNpzFileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(OmniSciNpyFileFormat, SdfFileFormat);
    SDF_DEFINE_FILE_FORMAT(OmniSciNpzFileFormat, SdfFileFormat);
}

PXR_NAMESPACE_CLOSE_SCOPE
