// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/// @file OmniSciPythonProxyFileFormatRegistration.cpp
///
/// Registers OmniSciPythonProxyFileFormat with the TfType / SdfFileFormat systems.
/// USD's plugin registry discovers this entry point via plugInfo.json at
/// load time; no user code needs to call it directly.

#include "OmniSciPythonProxyFileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(OmniSciPythonProxyFileFormat, SdfFileFormat);
}

PXR_NAMESPACE_CLOSE_SCOPE
