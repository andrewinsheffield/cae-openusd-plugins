<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# EnSight Gold and OpenUSD Conceptual Data Mapping

Resolver-backed case files are supported. Geometry and variable filenames
expanded from the case file are resolved relative to the original case asset;
directory enumeration is not required.

**Document Version:** 1.1.0
**Last Update:** 2026-06-30

## Introduction

### Overview

This document describes how the implemented `OmniSciEnSightFileFormat` plugin
maps EnSight Gold data structures to OpenUSD prims and properties. It is the
conceptual companion to the `OmniSciEnSight` schema library and the consumer
reference for the resulting USD representation.

The mapping is **one-way (EnSight → OpenUSD)**. The plugin is read-only;
round-trip write support is not in scope. The document records the supported
subset and current capability gaps.

EnSight Gold organises a dataset around a `.case` file that references geometry,
variables, and optional time metadata. OpenUSD organises data as a scenegraph of
typed prims plus composable API schemas. In this repository the mapping is built
through three custom schema plugins:

- **OmniSci** (`omni:sci:` namespace) — format-agnostic base schemas for
  scientific datasets, fields, and arrays.
- **OmniSciEnSight** (`omni:ensight:` namespace) — EnSight-specific API schemas
  for parts and pieces layered on top of the OmniSci base types.

Large array data (coordinates, connectivity, and variable arrays) is resolved
on demand by the same `CaeFileFormatData` backend that stores layer structure.

### References

This document has been prepared in reference to the software and specification
versions listed below.

#### EnSight Reference

| Version | Reference Documents |
|---------|---------------------|
| Gold format family | EnSight Gold case, geometry, and variable-file conventions implemented by the reader and exercised by `tests/data/EnSight/` |

#### OpenUSD Reference

| Version | Reference Documents |
|---------|---------------------|
| 24.08 | [OpenUSD C++ and Schema Documentation](https://openusd.org/release/api/index.html), [OpenUSD GitHub Repository](https://github.com/PixarAnimationStudios/OpenUSD), [USD Terms and Concepts](https://openusd.org/release/glossary.html) |

### General Assumptions and Constraints

**One-way mapping (EnSight → OpenUSD).** The plugin provides read-only
access. Writing OpenUSD back to EnSight is not supported.

**EnSight Gold binary only in v1.** The file format supports EnSight Gold
datasets whose referenced geometry and variable files use the binary layout. ASCII
variants and other EnSight families are out of scope for the initial version.

**`.case` as primary hook, `.encas` as alias.** The file format registers both
extensions, but `.case` is the main documented entrypoint.

**Schema composition over inheritance.** EnSight-specific semantics are expressed
through the `OmniSciDataset` and `OmniSciEnSightPiece` typed schemas together
with EnSight-specific APIs, with arrays carried by `OmniSciArrayAPI` and
variables by `OmniSciFieldAPI`.

**Lazy array loading.** Coordinate, connectivity, topology-count, and variable
arrays are loaded lazily. The `omni:sci:array:<name>:value`
attribute may be resolvable by USD value resolution without appearing in
`GetAuthoredProperties()`.

**Time handling is file-format-arg driven.** The reader parses the
`.case` `TIME` section and wildcarded filenames, then uses file-format arguments
such as `timeSource`, `timeScale`, and `timeOffset` (shared
`OmniSciFileFormatArgsTimeAPI`) to determine the USD time codes. The resolved
time codes are in canonical simulation seconds and the emitted layer sets
`timeCodesPerSecond = 1.0`; host pipelines that compose into a stage whose
TCPS is not 1 are responsible for authoring an
`Sdf.LayerOffset(scale=stage.GetTimeCodesPerSecond(), 0)` on the payload /
sublayer / reference arc -- the plugin does not ship a helper API for this.

**Root layout.** The default stage layout is `/<caseName>`,
matching the current repository conventions for CAE file formats.

**Prim name sanitisation.** Part descriptions, variable names, and any other
source-derived names are passed through `TfMakeValidIdentifier()`
before use as prim names or API instance names.

**Connectivity indexing is source-native.** EnSight connectivity is read
directly from the binary source and remains 1-based. Consumers that require
zero-based indices perform that conversion outside the file-format layer.

### Definitions, Acronyms, Abbreviations

| Term or Abbreviation | Description |
|----------------------|-------------|
| EnSight Gold | A CFD/CAE post-processing data format family centred on a `.case` file plus referenced geometry and variable files. |
| `.case` | EnSight case file describing geometry, variables, time sets, and referenced file naming patterns. |
| Part | An EnSight dataset partition with one coordinate set and one or more topology pieces. |
| Piece | One topology block within a part; usually all cells/faces of a single EnSight element type. |
| `nsided` | EnSight polygonal topology where each element carries a variable node count. |
| `nfaced` | EnSight polyhedral topology where each element carries a variable face count and each face carries a variable node count. |
| OmniSci | Format-agnostic USD schema plugin (`omni:sci:` namespace) providing base types for scientific datasets. |
| OmniSciEnSight | EnSight-specific USD schema plugin (`omni:ensight:` namespace) providing API schemas for parts and pieces. |
| API schema | A non-typed USD schema that adds a reusable set of properties to any prim via `apiSchemas`. |
| Lazy attribute | An attribute whose value is not stored in the primary layer but is resolved on demand from a lazy `SdfAbstractData` backend. |

---

## Concepts

The table below lists the top-level EnSight Gold concepts read by the plugin and
their OpenUSD equivalents.

| EnSight | OpenUSD | Description |
|---------|---------|-------------|
| [Case file (`.case` / `.encas`)](#case-file) | Root `UsdGeomScope` at `/<caseName>` (default prim) | Entry file that defines geometry, variables, and optional time metadata. |
| [Part](#part) | `OmniSciDataset` + `OmniSciEnSightUnstructuredPartAPI` + `OmniSciArrayAPI:coordinatesX/Y/Z` | One prim per EnSight part; owns shared coordinates, variables, and targets its topology pieces. |
| [Piece](#piece) | `OmniSciEnSightPiece` prim + `OmniSciEnSightUnstructuredPieceAPI` | One prim per EnSight topology block, with topology arrays carried directly as `OmniSciArrayAPI` instances. |
| [Variable entry (`scalar per node`, `vector per element`, etc.)](#variables) | `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` | One field/array pair per variable. |
| [TIME section](#time-varying-data) | USD time samples on `omni:sci:array:<name>:value` | Time metadata drives the time ordinates used for sampled variable arrays. |

### Case File

An EnSight Gold dataset is opened through a `.case` file. The case file declares:

- the geometry file name or wildcard pattern
- variable entries and their referenced file names
- optional time-set metadata

In OpenUSD the case file becomes the root of a composed dataset stage under
`/<caseName>`.

**Stage path:** `/<caseName>`

#### Properties

| EnSight | OpenUSD | Description |
|---------|---------|-------------|
| case file basename | Root prim name | Sanitised file stem used as the dataset root name. |
| GEOMETRY section | Child part prims | Drives creation of part datasets and their child piece prims. |
| VARIABLE section | `OmniSciFieldAPI` + `OmniSciArrayAPI` instances | Drives variable exposure on the appropriate owning dataset prim. |
| TIME section | USD time samples | Drives the time ordinals used for sampled lazy arrays. |

#### Composition

The root layout is:

```text
/<caseName>           UsdGeomScope (default prim)
```

`/<caseName>` is the layer's default prim and the parent of all imported
EnSight content.

---

### Part

An EnSight part groups a shared coordinate set and one or more topology pieces. In
the current mapping, each part becomes an `OmniSciDataset` prim with
`OmniSciEnSightUnstructuredPartAPI` applied. Shared point coordinates are authored
directly on the part prim via `OmniSciArrayAPI:coordinatesX`,
`OmniSciArrayAPI:coordinatesY`, and `OmniSciArrayAPI:coordinatesZ`.

**Stage path:** `/<caseName>/<partName>`

#### Properties

| EnSight | OpenUSD | Description |
|---------|---------|-------------|
| part description | Prim name | Sanitised part description from the geometry file. |
| part id | `omni:ensight:part:id` (int) | Numeric part id from the EnSight geometry file. |
| shared coordinates | `OmniSciArrayAPI:coordinatesX/Y/Z` + lazy attrs | Split coordinate arrays authored directly on the part prim. |
| pieces | `omni:ensight:part:pieces` (rel) | Relationships to the child piece prims. |

##### Property: `omni:ensight:part:id`

| Name | Data Type |
|------|-----------|
| EnSight part id | integer |
| OpenUSD `omni:ensight:part:id` | `int` |

##### Property: `omni:sci:array:coordinatesX:value`

| Name | Data Type |
|------|-----------|
| EnSight coordinate array | binary float32 array in the geometry file |
| OpenUSD `omni:sci:array:coordinatesX:value` | `float[]` in v1 |

##### Property: `omni:ensight:part:pieces`

Relationship from the part prim to the child piece prims representing the
topology blocks belonging to this part.

#### Composition

The part prim is defined with `OmniSciDataset::Define()` and then
`OmniSciEnSightUnstructuredPartAPI::Apply()` is called on the resulting prim.

---

### Piece

Each EnSight topology block within a part becomes a child `OmniSciEnSightPiece`
prim with `OmniSciEnSightUnstructuredPieceAPI` applied. A piece records its
element type and connectivity.

**Stage path:** `/<caseName>/<partName>/Piece_<n>`

#### Properties

| EnSight | OpenUSD | Description |
|---------|---------|-------------|
| piece order within part | Prim name | Stable synthetic name such as `Piece_0`, `Piece_1`, etc. |
| piece prim type | `OmniSciEnSightPiece` | Concrete typed schema used to identify EnSight topology blocks in the stage. |
| element type | `omni:ensight:piece:elementType` (token) | One of the supported EnSight topology tokens. |
| connectivity data | `OmniSciArrayAPI:connectivity` + lazy attr `omni:sci:array:connectivity:value` | Flat connectivity array for the piece. |

##### Property: `omni:ensight:piece:elementType`

Allowed tokens:

| Supported Tokens |
|------------------|
| `point`, `bar2`, `bar3`, `tria3`, `tria6`, `quad4`, `quad8`, `tetra4`, `tetra10`, `pyramid5`, `pyramid13`, `penta6`, `penta15`, `hexa8`, `hexa20`, `nsided`, `nfaced` |

##### Property: `omni:sci:array:connectivity:value`

| Name | Data Type |
|------|-----------|
| EnSight connectivity | integer array read from the geometry file |
| OpenUSD `omni:sci:array:connectivity:value` | `int[]` in v1 |

#### Metadata

`OmniSciArrayAPI::CreateDeviceAttr()` is `"cpu"` for connectivity
arrays.

#### Composition

The piece prim is defined with `OmniSciEnSightPiece::Define()`,
then `OmniSciEnSightUnstructuredPieceAPI::Apply()` is called. Piece-local
topology arrays such as `connectivity`, `elementNodeCounts`,
`elementFaceCounts`, and `faceNodeCounts` are authored directly as
`OmniSciArrayAPI` instances on the same prim and resolved lazily from the
geometry file.

---

### NSided Piece

An EnSight `nsided` piece stores variable-length polygons. In addition to the flat
connectivity array, it stores one count per element describing how many nodes belong
to each polygon.

#### Properties

| EnSight | OpenUSD | Description |
|---------|---------|-------------|
| node-count data | `OmniSciArrayAPI:elementNodeCounts` + lazy attr `omni:sci:array:elementNodeCounts:value` | Per-element polygon node counts. |

---

### NFaced Piece

An EnSight `nfaced` piece stores variable-length polyhedra. In addition to the flat
connectivity array, it stores:

- one count per element describing how many faces belong to each cell
- one count per face describing how many nodes belong to each face

In OpenUSD this metadata is carried by lazy count arrays on the same piece prim.

#### Properties

| EnSight | OpenUSD | Description |
|---------|---------|-------------|
| face-count data | `OmniSciArrayAPI:elementFaceCounts` + lazy attr `omni:sci:array:elementFaceCounts:value` | Number of faces per polyhedral element. |
| face-node-count data | `OmniSciArrayAPI:faceNodeCounts` + lazy attr `omni:sci:array:faceNodeCounts:value` | Number of nodes per face. |

---

### Variables

The EnSight `VARIABLE` section declares scalar, vector, or tensor data together with
its association (`per node` or `per element`) and referenced file name.

In OpenUSD each variable is represented by an `OmniSciFieldAPI:<name>` +
`OmniSciArrayAPI:<name>` pair.

#### Properties

| EnSight | OpenUSD | Description |
|---------|---------|-------------|
| variable name | `omni:sci:field:<name>:name` (string) | Original EnSight variable name before sanitisation. |
| variable association | `omni:sci:field:<name>:association` (token) | `"node"` for `per node`, `"element"` for `per element`. |
| variable data | `omni:sci:array:<name>:value` | Lazy variable array. |

#### Variable Ownership

The v1 ownership model is:

- **node variables** are authored on the part dataset
- **element variables** are authored on the part dataset

This keeps ownership stable even when a part has multiple pieces. If later EnSight
datasets require piece-local element ownership, that would be an extension to this
mapping rather than the v1 baseline.

##### Property: `omni:sci:field:<name>:association`

| EnSight declaration | OpenUSD token |
|---------------------|---------------|
| `scalar per node`, `vector per node`, `tensor ... per node` | `"node"` |
| `scalar per element`, `vector per element`, `tensor ... per element` | `"element"` |

#### Metadata

`OmniSciArrayAPI::CreateDeviceAttr()` is `"cpu"` for all variable
arrays in the initial implementation.

#### Composition

Variable arrays are read lazily from the referenced variable files.
For time-varying datasets, the same variable attribute path will carry multiple USD
time samples.

---

### Time-Varying Data

The EnSight `TIME` section controls how filename patterns expand across steps and how
those steps should be interpreted as USD time samples.

The time-related file format arguments are:

- `timeSource`
- `timeScale`
- `timeOffset`

The default `timeSource` is step indices.

#### Properties

| EnSight | OpenUSD | Description |
|---------|---------|-------------|
| `number of steps` | Number of authored time samples | Drives how many samples are registered for each sampled variable. |
| `filename start number` + `filename increment` | File-step expansion | Drives wildcard expansion for geometry and variable files. |
| physical time values | USD time ordinates | Selected with `timeSource = TimeValue` when the `time values:` block is present. |
| sampled variable arrays | USD time samples on `omni:sci:array:<name>:value` | One sample per selected time step. |

#### Composition

The time-ordinate policy is:

- if no TIME section exists, the dataset is non-time-varying by default
- if TIME exists, the reader expands wildcarded filenames and registers time samples
- `timeScale` and `timeOffset` are applied after the selected `timeSource` is
  resolved

---

## Appendices

### Appendix A: File Format Arguments

`OmniSciEnSightFileFormat` accepts the following public arguments when opening
a layer.

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `timeSource` | string | `"TimeStep"` | Native time source for sampled arrays: `"TimeStep"` uses zero-based step indices; `"TimeValue"` uses the explicit `time values:` block from the EnSight case file when present. Also reachable via `OmniSciFileFormatArgsTimeAPI:source`. |
| `timeScale` | float | 1.0 | Multiplier that converts the chosen source into simulation seconds (per-step `dt` for `TimeStep`; `1.0` when the `time values:` block stores seconds). Also reachable via `OmniSciFileFormatArgsTimeAPI:scale`. |
| `timeOffset` | float | 0.0 | Additive offset (simulation seconds) applied after `timeScale`. Also reachable via `OmniSciFileFormatArgsTimeAPI:offset`. |
| `cacheMode` | string | `"all"` | Lazy-array value retention policy: `"all"` caches sampled values; `"static"` and `"none"` do not retain sampled values. Also reachable via `OmniSciFileFormatArgsAPI:cacheMode`. |
| `ioThreads` | int | 1 | Number of worker threads used for chunked array reads. Also reachable via `OmniSciFileFormatArgsStreamingAPI:ioThreads`. |
| `mountPath` | string | filename-stem root | Absolute prim path used for sublayer placement. Flat layer argument only. |

### Appendix B: Stage Layout Example

An EnSight case `disk_out_ref.0.case` with one part (`fluid`), one `hexa8` piece,
and one nodal variable (`Temp_n`) maps to:

```text
/disk_out_ref_0                      UsdGeomScope  (default prim)
  /disk_out_ref_0/fluid              OmniSciDataset
                                       apiSchemas = ["OmniSciEnSightUnstructuredPartAPI",
                                                     "OmniSciArrayAPI:coordinatesX",
                                                     "OmniSciArrayAPI:coordinatesY",
                                                     "OmniSciArrayAPI:coordinatesZ",
                                                     "OmniSciFieldAPI:Temp_n",
                                                     "OmniSciArrayAPI:Temp_n"]
                                       omni:ensight:part:id = 1
                                       omni:ensight:part:pieces → [/disk_out_ref_0/fluid/Piece_0]
                                       omni:sci:array:coordinatesX:device = "cpu"
                                       omni:sci:array:coordinatesY:device = "cpu"
                                       omni:sci:array:coordinatesZ:device = "cpu"
                                       omni:sci:field:Temp_n:name = "Temp_n"
                                       omni:sci:field:Temp_n:association = "node"
                                       omni:sci:array:Temp_n:device = "cpu"
                                       -- lazy --
                                       omni:sci:array:coordinatesX:value = float[nNodes]
                                       omni:sci:array:coordinatesY:value = float[nNodes]
                                       omni:sci:array:coordinatesZ:value = float[nNodes]
                                       omni:sci:array:Temp_n:value = float[nNodes]

      /disk_out_ref_0/fluid/Piece_0      OmniSciEnSightPiece
                                                 apiSchemas = ["OmniSciEnSightUnstructuredPieceAPI",
                                                               "OmniSciArrayAPI:connectivity"]
                                                 omni:ensight:piece:elementType = "hexa8"
                                                 omni:sci:array:connectivity:device = "cpu"
                                                 -- lazy --
                                                 omni:sci:array:connectivity:value = int[...]
```

### Appendix C: Capability Gaps

The following EnSight concepts are outside the current mapping.

| EnSight Concept | Gap Description | Notes |
|-----------------|-----------------|-------|
| ASCII EnSight geometry/variable files | Not in scope for v1. | Initial implementation targets EnSight Gold binary only. |
| Piece-local element variables | Element variables are concatenated in piece order and authored on the part dataset. | Consumers use piece topology/count metadata to interpret ranges. |
| Unsupported variable declarations | Complex, constant, and unrecognized variable declarations are skipped. | Scalar/vector and supported tensor declarations are parsed. |
| Non-core metadata | Part extents and other optional headers are not preserved. | They could later be represented as dedicated attributes. |
