# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

message(STATUS "Configuring CAE USD schemas")

cae_add_schema(omniSci
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci"
)

cae_add_schema(omniSciCgns
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_cgns"
)

cae_add_schema(omniSciEnSight
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_ensight"
)

cae_add_schema(omniSciOpenFoam
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_openfoam"
)

cae_add_schema(omniSciVtk
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_vtk"
)

cae_add_schema(omniSciEdem
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_edem"
)

cae_add_schema(omniSciFlash
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_flash"
)

cae_add_schema(omniSciCae
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_cae"
)

cae_add_schema(omniSciReservoir
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_reservoir"
)

cae_add_schema(omniSciFileFormatArgs
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_file_format_args"
)
