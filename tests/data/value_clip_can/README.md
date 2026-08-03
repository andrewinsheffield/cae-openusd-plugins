<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Value Clip Can Sample

This fixture is derived from ParaView's `can.ex2` example dataset. The source
was merged to one unstructured grid and split into a static mesh file plus short
NPZ and VTK sequences for value-clip testing and documentation.

Files:

- `mesh.vtu` - merged unstructured grid at source timestep 0, with result
  arrays stripped. It is intended to be composed as static topology/geometry.
- `fields_###.npz` - per-timestep field arrays split into scalar components.
- `fields_manifest.usda` - value-clip manifest for the NPZ field arrays.
- `root.usda` - example USD layer composing `mesh.vtu` as a reference and the
  NPZ files as a `fields` value clip set. This layer owns the stable field
  metadata: `cell_eqps` is an element field, and the `point_*` arrays are node
  fields. It also declares empty typed `omni:sci:array:*:value` attributes so
  standard USD consumers see valid attributes while value clips provide the
  samples.
- `moving_points_###.vtk` - legacy VTK point-only files with source
  displacement applied to the static mesh points.
- `moving_points_manifest.usda` - value-clip manifest for the clipped points
  array.
- `moving_points_root.usda` - example USD layer composing `mesh.vtu` as static
  topology and the VTK files as a `points` value clip set.

Selected source times: `0, 0.00100000598, 0.00199998566, 0.00300002052`

Field arrays:

- `cell_eqps`
- `point_accl_x`
- `point_accl_y`
- `point_accl_z`
- `point_displ_x`
- `point_displ_y`
- `point_displ_z`
- `point_vel_x`
- `point_vel_y`
- `point_vel_z`

`root.usda` and `moving_points_root.usda` are executable value-clip fixtures.
The NPZ fixture varies fields over static geometry; the VTK fixture varies
points over static topology.
