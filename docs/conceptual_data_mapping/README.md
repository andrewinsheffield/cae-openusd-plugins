<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Conceptual Data Mappings

A Conceptual Data Mapping describes how concepts from a source format are
represented in OpenUSD. This is a USD-facing semantic contract: it defines prim
hierarchy, applied schemas, properties, relationships, array types and shapes,
time samples, source-fidelity guarantees, and capability gaps.

These documents are not source-format specifications, reader implementation
manuals, or generated schema API references.

## Mappings

| Source domain | Mapping |
| --- | --- |
| CGNS | [CGNS and OpenUSD](cgns.md) |
| EDEM | [EDEM and OpenUSD](edem.md) |
| FLASH AMR | [FLASH AMR and OpenUSD](flash.md) |
| EnSight Gold | [EnSight Gold and OpenUSD](ensight.md) |
| Eclipse reservoir data | [GRDECL, EGRID, INIT, UNRST and OpenUSD](grdecl_egrid.md) |
| OpenFOAM | [OpenFOAM and OpenUSD](openfoam.md) |
| VTK | [VTK and OpenUSD](vtk.md) |
| NumPy | [NPY/NPZ and OpenUSD](numpy.md) |
| Trimesh surface formats | [Trimesh and OpenUSD](trimesh.md) |
| NanoVDB | [NanoVDB and OpenUSD](nanovdb.md) |

The generic Python proxy has no fixed mapping because its selected module owns
the authored hierarchy and semantics. See the
[Python proxy contract](../file_formats/python_proxy.md).

## Asset Resolution and Dataset Layout

File-format plugins access named assets through OpenUSD's Asset Resolver. The
application is responsible for installing a resolver that understands the
identifier scheme in the stage, such as `omniverse://`. The plugins do not
depend on a storage-specific client library.

The resolver API can resolve and open a named asset, but it cannot portably list
a directory. This distinction determines which dataset layouts can be loaded
from resolver-backed storage:

| Source domain | Resolver-backed support |
| --- | --- |
| CGNS, VTK, NPY/NPZ, NanoVDB, Trimesh | Supported for self-contained files |
| EGRID, INIT, UNRST | Supported as independently opened files |
| GRDECL | Supported, including explicit `INCLUDE` paths resolved relative to the deck |
| EnSight Gold | Supported; geometry and variable filenames expanded from the case file are resolved relative to the case asset |
| FLASH AMR | Supported with explicit `file` or `files` descriptor entries; wildcard `pattern` descriptors require a local filesystem |
| OpenFOAM | Local filesystem only because loading requires case-directory and time-directory scanning |
| EDEM | Local filesystem only because loading requires `<case>_data` directory scanning |
| Python proxy | The root file is resolver-backed; selected modules must remain self-contained because callbacks receive a local path |

Self-contained support does not extend to implicit sidecars hidden behind a
third-party library, such as external links that the plugin cannot identify and
resolve individually. Multi-file formats are resolver-backed only when the
root file names every child asset or provides a deterministic filename pattern
that does not require directory enumeration.

When a directory-oriented layout is opened through a non-filesystem resolver,
the reader reports the layout as unsupported and explains that the complete
dataset must be copied to a local filesystem or represented by an explicit
manifest. It must not silently reinterpret a URL as a native path.

## Required Content

Each mapping should state:

1. supported source concepts and entry extensions;
2. resulting USD hierarchy and default prim;
3. applied typed and API schemas;
4. source-concept to USD-property mapping;
5. array types, shapes, association, and indexing conventions;
6. time-varying behavior;
7. file-format arguments that affect the mapping;
8. source-fidelity guarantees and deliberate conversions;
9. unsupported concepts and known limitations;
10. a representative USDA or stage-tree example.
11. resolver-backed access and any dataset-layout constraints.

Claims must be verified against schema sources, reader code, plugin
registration, and tests rather than copied from historical design notes.
