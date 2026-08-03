# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cae_add_file_format(omniSciOpenFoamFileFormat
    ENABLE_VAR CAE_ENABLE_OPENFOAM
    DEFAULT    ON
    SOURCES
        src/OmniSciOpenFoamFileFormat.cpp
        src/OmniSciOpenFoamFileFormatRegistration.cpp
    USD_LIBRARIES
        pcp
        usdGeom
        work
    LIBRARIES
        omniSciFileFormatShared
        omniSciFileFormatArgs
        omniSciOpenFoam
        omniSci
    PLUGIN_DEPS
        omniSciFileFormatArgs
        omniSciOpenFoam
        omniSci
)
