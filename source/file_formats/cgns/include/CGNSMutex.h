// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>

/// Process-wide mutex that must be held for every CGNS / CGIO library call.
/// CGNS is not thread-safe; all cg_* and cgio_* calls must be serialised.
std::mutex& GetCGNSMutex();
