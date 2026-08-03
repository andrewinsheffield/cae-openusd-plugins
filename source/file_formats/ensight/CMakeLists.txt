# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cae_add_file_format(omniSciEnSightFileFormat
    ENABLE_VAR CAE_ENABLE_ENSIGHT
    DEFAULT    ON
    SOURCES
        src/OmniSciEnSightFileFormat.cpp
        src/OmniSciEnSightFileFormatRegistration.cpp
    USD_LIBRARIES
        pcp
        usdGeom
        work
    LIBRARIES
        omniSciFileFormatShared
        omniSciFileFormatArgs
        omniSciEnSight
        omniSci
    PLUGIN_DEPS
        omniSciFileFormatArgs
        omniSciEnSight
        omniSci
)
