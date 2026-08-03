<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Python Proxy File Format

The `.pydf` reader delegates structure discovery and array loading to a Python
module while retaining the C++ `SdfFileFormat` and `CaeFileFormatData` integration.
It is an adapter mechanism, not a fixed Conceptual Data Mapping: the selected
module defines the hierarchy and semantics it authors.

## Arguments

| Argument | Default | Purpose |
| --- | --- | --- |
| `pythonModule` | Required | Importable module containing the adapter callbacks. |
| `pythonPath` | Empty | Directory prepended to `sys.path` before import. |
| `pythonReadFunction` | `read` | Structure/manifest callback name. |
| `pythonCanReadFunction` | `can_read` | Optional probe callback name. |
| `pythonLoadArrayFunction` | `load_array` | Lazy value callback name. |
| `cacheMode` | `all` | Lazy value-retention policy. |
| `mountPath` | Filename-stem root | Optional flat sublayer placement. |

The typed `OmniSciFileFormatArgsPythonAPI` exposes the module and callback
arguments on payload-bearing prims.

## Callback Contract

The read callback receives the target layer, resolved path, metadata-only flag,
and argument dictionary. It authors structure into the supplied layer and may
return a lazy-field manifest describing attribute paths, source tokens, types,
and available time samples. When USD requests one of those values,
`PythonFileFormatBase` invokes the configured array loader.

A manifest `token` without an explicit sample list is registered as a
single-state sample at time `0`. Lazy values do not author defaults, so callers
must request an explicit time code or `Usd.TimeCode.EarliestTime()`.

NPY, NPZ, Trimesh, and NanoVDB use fixed modules built on this same mechanism.
Their Conceptual Data Mappings are stable because the repository owns those
modules; a generic `.pydf` adapter must document its own mapping.
