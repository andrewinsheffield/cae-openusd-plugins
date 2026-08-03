<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Trimesh and OpenUSD Conceptual Data Mapping

Resolver-backed identifiers are supported for the self-contained `.stl`,
`.ply`, and `.3mf` inputs accepted by this plugin.

The `omniSciTrimeshFileFormat` plugin reads `.stl`, `.ply`, and `.3mf` through
the Python `trimesh` package. It maps one source mesh to one triangular
`OmniSciDataset` with `OmniSciCaeMeshAPI`.

```text
/<filenameStem>  OmniSciDataset (default prim)
  OmniSciCaeMeshAPI
  OmniSciArrayAPI:points
  OmniSciArrayAPI:faceVertexIndices
  OmniSciArrayAPI:faceVertexCounts
```

| Source concept | USD value |
| --- | --- |
| Vertices | `omni:sci:array:points:value` as `float3[]` |
| Triangle indices | `omni:sci:array:faceVertexIndices:value` as flattened `int[]` |
| Face sizes | `omni:sci:array:faceVertexCounts:value` as `int[]`, one value of `3` per face |

Stage structure can be discovered without importing `trimesh`; the dependency
is required when values are requested. Vertices are converted to float32 and
faces to int32. Materials, texture coordinates, normals, scenes containing
multiple meshes, non-triangular source topology, and format-specific metadata
are not represented.

`cacheMode` and `mountPath` have their common meanings. All three arrays are
single samples at time 0.
