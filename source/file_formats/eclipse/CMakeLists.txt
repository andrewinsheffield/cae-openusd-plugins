# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cae_add_file_format(omniSciEclipseFileFormat
    ENABLE_VAR CAE_ENABLE_ECLIPSE
    DEFAULT    ON
    SOURCES
        src/EclipseBinaryFile.cpp
        src/OmniSciEclipseFileFormatRegistration.cpp
        src/OmniSciGrdeclFileFormat.cpp
        src/OmniSciEgridFileFormat.cpp
        src/OmniSciInitFileFormat.cpp
        src/OmniSciUnrstFileFormat.cpp
    USD_LIBRARIES
        pcp
        usdGeom
    LIBRARIES
        omniSciFileFormatShared
        omniSciFileFormatArgs
        omniSciReservoir
        omniSci
    PLUGIN_DEPS
        omniSciFileFormatArgs
        omniSciReservoir
        omniSci
)
