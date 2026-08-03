// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CGNSMutex.h"

std::mutex& GetCGNSMutex()
{
    static std::mutex mutex; // NOSONAR: block-scope variables cannot be inline.
    return mutex;
}
