# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if(Python3_FOUND)
    cae_add_file_format(omniSciPythonProxyFileFormat
        ENABLE_VAR CAE_ENABLE_PYTHON_PROXY
        DEFAULT    ON
        SOURCES
            src/OmniSciPythonProxyFileFormat.cpp
            src/OmniSciPythonProxyFileFormatRegistration.cpp
        USD_LIBRARIES
            pcp
            usd
        LIBRARIES
            omniSciFileFormatPythonShared
            omniSciFileFormatArgs
        PLUGIN_DEPS
            omniSciFileFormatArgs
    )
endif()
