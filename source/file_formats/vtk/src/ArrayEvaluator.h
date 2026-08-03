// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file ArrayEvaluator.h
///
/// Runtime bridge from ArraySpec metadata to lazy-loaded USD-facing VtValues.

#include "DatasetSpec.h"
#include "DisablePXRWarnings.h"
#include "FileHandle.h"
#include "ScalarPayloadReader.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <memory>

namespace cae::vtk
{

/// Evaluate one ArraySpec into its final USD-facing value.
class ArrayEvaluator
{
public:
    explicit ArrayEvaluator(FileHandle& file);

    /// Evaluate `array` into its final USD-facing value.
    ///
    /// This reads the scalar payload through the configured payload reader,
    /// then applies any required lazy transformation such as vector
    /// reinterpretation, XML offset normalization, or legacy packed-cell
    /// topology derivation.
    PXR_NS::VtValue Evaluate(const ArraySpec& array) const;

private:
    FileHandle& _file;
};

/// Construct a per-payload reader for the payload spec.
std::unique_ptr<ScalarPayloadReader> MakeScalarPayloadReader(const ScalarPayloadSpec& payload);

} // namespace cae::vtk
