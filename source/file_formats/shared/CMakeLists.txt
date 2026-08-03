# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cae_usd_runtime_library(omniSciFileFormatShared
    SOURCES
        CaeFileFormatData.cpp
        ResolverAsset.cpp
    EXPORT_DEFINE
        OMNI_SCI_FILE_FORMAT_SHARED_EXPORTS
    PUBLIC_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}"
    SYSTEM_PRIVATE_INCLUDE_DIRS
        ${USD_INCLUDE_DIRS}
    SYSTEM_INTERFACE_INCLUDE_DIRS
        ${USD_INCLUDE_DIRS}
    USD_LIBRARIES
        ar
        arch
        sdf
)

if(Python3_FOUND)
    cae_usd_runtime_library(omniSciFileFormatPythonShared
        SOURCES
            PythonFileFormatBase.cpp
        EXPORT_DEFINE
            OMNI_SCI_FILE_FORMAT_PYTHON_SHARED_EXPORTS
        PUBLIC_INCLUDE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}"
        SYSTEM_PRIVATE_INCLUDE_DIRS
            ${Python3_INCLUDE_DIRS}
            ${USD_INCLUDE_DIRS}
        SYSTEM_INTERFACE_INCLUDE_DIRS
            ${Python3_INCLUDE_DIRS}
            ${USD_INCLUDE_DIRS}
        USD_LIBRARIES
            plug
            sdf
            usd
        LIBRARIES
            Python3::Python
            omniSciFileFormatShared
    )
endif()
