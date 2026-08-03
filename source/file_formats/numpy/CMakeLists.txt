# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if(Python3_FOUND)
    cae_add_file_format(omniSciNumpyFileFormat
        ENABLE_VAR CAE_ENABLE_NUMPY
        DEFAULT    ON
        SOURCES
            src/OmniSciNumpyFileFormatRegistration.cpp
            src/OmniSciNpyFileFormat.cpp
            src/OmniSciNpzFileFormat.cpp
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
            cae_npy
            cae_npz
    )
endif()
