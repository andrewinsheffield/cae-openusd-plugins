// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "OmniSciFileFormatSharedAPI.h"

#include <stdexcept>

namespace cae
{

/// Reports a failure while resolving, parsing, or reading a CAE file format.
class OMNI_SCI_FILE_FORMAT_SHARED_TYPE FileFormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

} // namespace cae
