# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

message(STATUS "Configuring CAE file format plugins")

add_subdirectory(shared)
add_subdirectory(ensight)
add_subdirectory(openfoam)
add_subdirectory(eclipse)
add_subdirectory(vtk)
add_subdirectory(vtkhdf)
add_subdirectory(edem)
add_subdirectory(flash)
add_subdirectory(cgns)
add_subdirectory(python_proxy)
add_subdirectory(numpy)
add_subdirectory(trimesh)
add_subdirectory(nvdb)
