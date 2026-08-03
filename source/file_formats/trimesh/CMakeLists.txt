# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if(Python3_FOUND)
    cae_add_file_format(omniSciTrimeshFileFormat
        ENABLE_VAR CAE_ENABLE_TRIMESH
        DEFAULT    ON
        SOURCES
            src/OmniSciTrimeshFileFormat.cpp
            src/OmniSciTrimeshFileFormatRegistration.cpp
        USD_LIBRARIES
            pcp
            usd
        LIBRARIES
            omniSciFileFormatPythonShared
            omniSciFileFormatArgs
            omniSciCae
            omniSci
        PLUGIN_DEPS
            omniSciFileFormatArgs
            omniSciCae
            omniSci
        PYTHON_MODULES
            cae_trimesh
    )
endif()
