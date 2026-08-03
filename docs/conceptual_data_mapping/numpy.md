<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# NumPy and OpenUSD Conceptual Data Mapping

Resolver-backed identifiers are supported for self-contained NPY and NPZ
files.

The `omniSciNumpyFileFormat` plugin exposes `.npy` and `.npz` files as
read-only `OmniSciDataset` layers. Structure is derived from NumPy headers and
NPZ member headers without loading the array payloads; values are loaded on
demand by the Python adapter.

## Common Layout

The default prim is `/<filenameStem>`. Names are sanitized for USD. Array
instances use `omni:sci:array:<instance>:value` and preserve supported NumPy
floating-point and integer widths:

| NumPy dtype/shape | USD type |
| --- | --- |
| 32-bit-or-smaller float | `float[]` |
| 64-bit float | `double[]` |
| 32-bit-or-smaller integer | `int[]` |
| 64-bit integer | `int64[]` |
| Last dimension 2, 3, or 4 | Corresponding vector array when supported |

Unsupported dtypes are rejected for NPY and skipped where a selected NPZ
mapping cannot represent them. `allowPickle` defaults to false.

## NPY

An NPY file contributes one raw `OmniSciArrayAPI` instance to the dataset.
No field, point-cloud, mesh, or CGNS semantics are inferred.

```text
/<filenameStem>  OmniSciDataset (default prim)
  OmniSciArrayAPI:<arrayName>
  omni:sci:array:<arrayName>:value
```

`arrayName` defaults to `array`.

## NPZ Mapping Modes

The `schema` argument selects one of three mappings.

### `Point Cloud` (default)

The adapter resolves coordinates from explicit `coordsArray` or
`coordsArrayX/Y/Z` arguments, then common coordinate names. It applies
`OmniSciCaePointCloudAPI`, exposes `pointsX`, `pointsY`, and `pointsZ`, and
maps remaining supported one-dimensional arrays to paired field/array APIs with
`association = "node"`.

### `CGNS`

The adapter authors a single-base, single-zone CGNS hierarchy:

```text
/<filenameStem>                     Scope (default prim)
  /Base                             CGNSBase
    /Zone                           CGNSZone + OmniSciCgnsZoneAPI
      /GridCoordinates              coordinate arrays
      /FlowSolution                 field arrays
      /Section                      unstructured element metadata/arrays
```

Coordinate, connectivity, element type, and element-range roles are recognized
from documented member-name aliases. This is a synthetic CGNS-shaped mapping;
it is not a CGNS file reader.

### `None`

Every supported one-dimensional member becomes an `OmniSciArrayAPI` instance.
No geometry marker or `OmniSciFieldAPI` is applied. Multi-dimensional members
are skipped.

## Arguments

| Argument | Meaning |
| --- | --- |
| `schema` | `Point Cloud`, `CGNS`, or `None`. |
| `coordsArray` | Interleaved coordinate member. |
| `coordsArrayX/Y/Z` | Split coordinate members. |
| `arrayName` | NPY array instance name. |
| `allowPickle` | Permit NumPy pickle loading; default false. |
| `cacheMode`, `mountPath` | Common lazy/cache and placement controls. |

## Capability Boundaries

- NPY intentionally carries no inferred field semantics.
- Point-cloud mode maps only supported one-dimensional non-coordinate fields.
- `None` mode skips multidimensional members.
- CGNS mode covers one base, one zone, one coordinate set, one flow solution,
  and one element section.
