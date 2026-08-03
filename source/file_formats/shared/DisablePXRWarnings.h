// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CAE_FILE_FORMATS_DISABLE_PXR_WARNINGS_H
#define CAE_FILE_FORMATS_DISABLE_PXR_WARNINGS_H

#if defined(__clang__)
#    define CAE_DISABLE_PXR_WARNINGS_BEGIN                                                                             \
        _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wcpp\"")
#    define CAE_DISABLE_PXR_WARNINGS_END _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
#    define CAE_DISABLE_PXR_WARNINGS_BEGIN _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wcpp\"")
#    define CAE_DISABLE_PXR_WARNINGS_END _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#    define CAE_DISABLE_PXR_WARNINGS_BEGIN                                                                             \
        __pragma(warning(push)) __pragma(warning(disable : 4244)) __pragma(warning(disable : 4267))                    \
            __pragma(warning(disable : 4996))
#    define CAE_DISABLE_PXR_WARNINGS_END __pragma(warning(pop))
#else
#    define CAE_DISABLE_PXR_WARNINGS_BEGIN
#    define CAE_DISABLE_PXR_WARNINGS_END
#endif

#endif
