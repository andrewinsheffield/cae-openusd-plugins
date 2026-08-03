# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if(Python3_FOUND)
    cae_add_file_format(omniSciNvdbFileFormat
        ENABLE_VAR CAE_ENABLE_NVDB
        DEFAULT    ON
        SOURCES
            src/OmniSciNvdbFileFormat.cpp
            src/OmniSciNvdbFileFormatRegistration.cpp
        USD_LIBRARIES
            pcp
            usd
        LIBRARIES
            omniSciFileFormatPythonShared
            omniSciFileFormatArgs
            omniSci
        PLUGIN_DEPS
            omniSciFileFormatArgs
            omniSci
        PYTHON_MODULES
            cae_nvdb
    )
endif()
