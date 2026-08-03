<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# OpenFOAM and OpenUSD Conceptual Data Mapping

OpenFOAM case loading requires directory enumeration for `constant/polyMesh`
and time directories. It is therefore supported only on a local filesystem.
Opening a resolver-backed case reports this layout as unsupported instead of
treating the resolved identifier as a native path.

**Document Version:** 1.0.0
**Last Update:** 2026-04-17

## Introduction

### Overview

This document describes how OpenFOAM case data is translated into OpenUSD prims
and properties by the `OmniSciOpenFoamFileFormat` plugin. It is the authoritative
reference for developers working on the plugin and for consumers reading the
OpenUSD representation of OpenFOAM data.

The mapping is **one-way (OpenFOAM → OpenUSD)**. The plugin is read-only;
round-trip write support is not in scope. The document notes where OpenFOAM
concepts have no OpenUSD representation (capability gaps).

An OpenFOAM case is a directory tree rather than a single file. A `.foam`
sentinel file inside the case directory anchors the USD layer; the plugin then
parses `constant/polyMesh/` for geometry and topology and scans numeric
time-step directories for per-cell (cell-centred) internal fields. OpenUSD
organises the result as a scenegraph of typed prims plus composable API
schemas. The mapping uses two custom schema plugins:

- **OmniSci** (`omni:sci:` namespace) — format-agnostic base schemas for
  scientific datasets, fields, and arrays.
- **OmniSciOpenFoam** (`omni:foam:` namespace) — OpenFOAM-specific API schemas
  for the polyhedral mesh and boundary patches, layered on top of the OmniSci
  base types.

Large array data (points, face connectivity, owner/neighbour, internal fields)
is not loaded eagerly. One `CaeFileFormatData` backend stores structure and
resolves sampled `omni:sci:array:<name>:value` attributes on demand.

### References

This document has been prepared in reference to the software and
specification versions listed below.

#### OpenFOAM Reference

| Version | Reference Documents |
|---------|---------------------|
| OpenFOAM v2312 / OpenFOAM 11 | [OpenFOAM User Guide — Mesh description](https://www.openfoam.com/documentation/user-guide/4-mesh-generation-and-conversion/4.1-mesh-description), [polyMesh directory contents](https://www.openfoam.com/documentation/user-guide/4-mesh-generation-and-conversion/4.2-mesh-generation-with-the-blockmesh-utility) |

#### OpenUSD Reference

| Version | Reference Documents |
|---------|---------------------|
| 24.08 | [OpenUSD C++ and Schema Documentation](https://openusd.org/release/api/index.html), [OpenUSD GitHub Repository](https://github.com/PixarAnimationStudios/OpenUSD), [USD Terms and Concepts](https://openusd.org/release/glossary.html) |

### General Assumptions and Constraints

**One-way mapping (OpenFOAM → OpenUSD).** The plugin provides read-only access.
Writing OpenUSD back to OpenFOAM is not supported.

**`.foam` sentinel file as the entry point.** The plugin registers the `foam`
extension. The `.foam` file itself need not contain any content; its parent
directory is treated as the OpenFOAM case root, and the plugin reads
`constant/polyMesh/` and numeric time directories relative to that root.

**polyMesh topology only in v1.** The reader consumes the five standard
`constant/polyMesh/` files — `points`, `faces`, `owner`, `neighbour`, and
`boundary` — which together describe a polyhedral face-based mesh.
`pointZones`, `faceZones`, `cellZones`, parallel-decomposition
`processor*/polyMesh/` subdirectories, dynamic-mesh data, and `lagrangian/`
(particle) data are not read.

**ASCII and binary formats.** Both ASCII and little/big-endian binary polyMesh
and internal-field files are supported. The label size (`label=32|64`) and
scalar size (`scalar=32|64`) are read from the `arch` header entry on binary
files; 32-bit floats are the canonical USD target type regardless of the source
width. Compressed files (`.gz`) are not yet supported.

**Cell-centred internal fields only in v1.** Each file in a numeric time
directory is inspected for an `internalField` entry. Files that parse
successfully as `volScalarField` or `volVectorField` are exposed as lazy
`OmniSciArrayAPI` + `OmniSciFieldAPI` pairs with `association = "element"`.
Boundary-field values, tensor/symmTensor fields, sphericalTensorField, and
surface-field (`surfaceScalarField`, `surfaceVectorField`) variants are not yet
surfaced — files that fail inspection are skipped silently.

**Lazy array loading.** Points, face connectivity, face offsets, owner,
neighbour, and internal-field arrays are registered as lazy attributes; data
is fetched from the originating polyMesh or time-directory file on first
access. The `omni:sci:array:<name>:value` attribute on a prim is intentionally
absent from `GetAuthoredProperties()` but resolvable via value resolution.

**Schema composition over inheritance.** OpenFOAM-specific
attributes are added to prims via API schemas, not by subclassing a base prim
type. The mesh prim is an `OmniSciDataset` with `OmniSciOpenFoamPolyMeshAPI`
applied; each boundary patch prim is an `OmniSciDataset` with
`OmniSciOpenFoamBoundaryPatchAPI` applied. Connectivity-carrying
`OmniSciArrayAPI` instances use OpenFOAM-specific names (`points`, `faces`,
`facesOffsets`, `owner`, `neighbour`) defined in the `omniSciOpenFoam` library
tokens. `OmniSciCaeMeshAPI` is **not** applied, because the OpenFOAM
polyhedral topology is face-based rather than face-vertex-count based.

**Root layout.** The default stage layout is `/<caseStem>/Volume` plus
one `/<caseStem>/Boundaries/<patch>` prim per boundary patch, where
`<caseStem>` is the sanitised filename stem of the `.foam` sentinel.

**Up-axis.** The reader calls `UsdGeomSetStageUpAxis(Z)` to match the
OpenFOAM convention (gravity along −Z for most external-flow solvers).

**Prim and instance name sanitisation.** OpenFOAM patch names, field file
names, and the implicit root name (derived from the `.foam` file stem) are
passed through `TfMakeValidIdentifier()` before use as prim names or API
schema instance names. Original names are preserved as attribute values (patch
`omni:foam:patch:name`; field `omni:sci:field:<name>:name`).

### Definitions, Acronyms, Abbreviations

| Term or Abbreviation | Description |
|----------------------|-------------|
| OpenFOAM | Open-source Field Operation And Manipulation — a C++ CFD toolbox organised around case directories. |
| Case | A directory tree containing an OpenFOAM dataset (mesh, boundary conditions, fields, solver settings). |
| `.foam` | Empty sentinel file inside a case directory used by ParaView, Kit-CAE, and this plugin as the open hook. |
| polyMesh | OpenFOAM's polyhedral face-based mesh representation, stored under `constant/polyMesh/`. |
| `points` | File holding vertex coordinates (one `vectorField`). |
| `faces` | File holding face-vertex connectivity. May be `faceList` (per-face counts + indices) or binary `faceCompactList` (offsets + flat indices). |
| `owner` / `neighbour` | Per-face cell ids. Internal faces have both; boundary faces have only `owner`. |
| `boundary` | File declaring named boundary patches, each with a type, `startFace`, and `nFaces`. |
| Time directory | A numeric directory name at the case root (e.g. `0/`, `0.001/`, `100/`) holding the fields at that step. |
| `internalField` | A cell-centred field value block inside a field file; may be `uniform <value>` or `nonuniform List<scalar|vector>`. |
| `FoamFile` header | The dictionary block prefixing every OpenFOAM text/binary file, declaring `version`, `format`, `class`, `arch`, and related metadata. |
| OmniSci | Format-agnostic USD schema plugin (`omni:sci:` namespace) providing base types for scientific datasets. |
| OmniSciOpenFoam | OpenFOAM-specific USD schema plugin (`omni:foam:` namespace) providing API schemas for polyhedral meshes and boundary patches. |
| API schema | A non-typed USD schema that adds a reusable set of properties to any prim via `apiSchemas`. |
| Lazy attribute | An attribute whose value is not stored in the primary layer but is resolved on demand from a `CaeFileFormatData` sublayer. |

---

## Concepts

The table below lists the OpenFOAM concepts consumed by the v1 reader and
their OpenUSD equivalents. Gaps are flagged where the OpenFOAM concept has no
OpenUSD representation today.

| OpenFOAM | OpenUSD | Description |
|----------|---------|-------------|
| [Case root (`.foam` + directory tree)](#case-root) | `UsdGeomScope` at `/<caseStem>` (default prim) | Entry layer; root scope carries the entire case under it. |
| [polyMesh volume](#polymesh-volume) | `OmniSciDataset` + `OmniSciOpenFoamPolyMeshAPI` + `OmniSciArrayAPI:points/faces/facesOffsets/owner/neighbour` | One prim per case; owns all mesh and internal-field arrays. |
| [Boundary patch (`boundary` entry)](#boundary-patch) | Child `OmniSciDataset` + `OmniSciOpenFoamBoundaryPatchAPI` | One prim per patch under `/<caseStem>/Boundaries/`. |
| [Internal scalar/vector field](#internal-fields) | `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` on the volume prim | Cell-centred field; lazy array resolved per time step. |
| [Time directories](#time-varying-data) | USD time samples on `omni:sci:array:<name>:value` | One sample per time directory where the field file is present. |
| Boundary-field values (patch-local field data) | | **Not implemented.** Only `internalField` blocks are read. |
| `pointZones` / `faceZones` / `cellZones` | | **Not implemented.** Named zone groupings are not read. |
| `processor*/polyMesh/` (parallel decomposition) | | **Not implemented.** Only the reconstructed mesh is consumed. |
| Dynamic mesh (`pointMotionU`, `dynamicMeshDict`) | | **Not implemented.** |
| `lagrangian/` particle data | | **Not implemented.** |

### Case Root

An OpenFOAM case is opened via the `.foam` sentinel file. The sentinel itself
is not parsed; its parent directory is the case root and the plugin expects
the standard OpenFOAM layout:

```
<caseDir>/
    <caseDir>/constant/polyMesh/{points, faces, owner, neighbour, boundary}
    <caseDir>/<t0>/<field>          (optional; numeric time directories)
    <caseDir>/<t1>/<field>
    ...
```

In OpenUSD the case is rooted at `/<caseStem>` (the layer's default prim),
where `<caseStem>` is the sanitised filename stem of the `.foam` file.

**Stage path:** `/<caseStem>`

#### Composition

```text
/<caseStem>                      UsdGeomScope  (default prim, Z-up; case root container)
/<caseStem>/Volume               OmniSciDataset  (polyMesh volume)
/<caseStem>/Boundaries           UsdGeomScope   (patch container)
/<caseStem>/Boundaries/<patch>   OmniSciDataset (one per boundary patch)
```

The root scope has no authored properties of its own in v1; it exists as a
fixed anchor for the volume and boundary prims.

---

### polyMesh Volume

The OpenFOAM polyhedral mesh is exposed as a single `OmniSciDataset` prim at
`/<caseStem>/Volume`. `OmniSciOpenFoamPolyMeshAPI` is applied as the
marker API schema. All five polyMesh arrays are authored as
`OmniSciArrayAPI` instances using the instance names declared in the
`omniSciOpenFoam` library tokens:

**Stage path:** `/<caseStem>/Volume`

#### Properties

| OpenFOAM | OpenUSD | Description |
|----------|---------|-------------|
| `constant/polyMesh/points` | `OmniSciArrayAPI:points` + lazy attr `omni:sci:array:points:value` | Vertex coordinates (`vectorField`). Exposed as `float3[]` regardless of the file's `scalar=32|64`. |
| `constant/polyMesh/faces` (vertex indices) | `OmniSciArrayAPI:faces` + lazy attr `omni:sci:array:faces:value` | Flat concatenation of face-vertex indices across all faces. `int[]`. |
| `constant/polyMesh/faces` (face offsets) | `OmniSciArrayAPI:facesOffsets` + lazy attr `omni:sci:array:facesOffsets:value` | CSR-style offsets of length `numFaces + 1`; the k-th face owns indices `[facesOffsets[k], facesOffsets[k+1])`. `int[]`. |
| `constant/polyMesh/owner` | `OmniSciArrayAPI:owner` + lazy attr `omni:sci:array:owner:value` | Per-face owning-cell id; length equals total number of faces. `int[]`. |
| `constant/polyMesh/neighbour` | `OmniSciArrayAPI:neighbour` + lazy attr `omni:sci:array:neighbour:value` | Per-face neighbour-cell id; length equals number of internal faces. `int[]`. |

##### Property: `omni:sci:array:points:value`

| Name | Data Type |
|------|-----------|
| OpenFOAM points | `vectorField` — binary float32 / float64 or ASCII (x y z) triples |
| OpenUSD `omni:sci:array:points:value` | `float3[]` in v1 (float64 sources are narrowed to float32) |

##### Property: `omni:sci:array:faces:value` and `omni:sci:array:facesOffsets:value`

OpenFOAM stores face connectivity in two on-disk layouts:

- **`faceList`** — per-face `(vertexCount, vertexIndex_0, …)` entries. The
  reader expands these into two arrays: a flat index array (`faces`) and a
  CSR offset array (`facesOffsets`).
- **`faceCompactList`** (binary only) — two consecutive `labelList` blocks,
  offsets first then flat indices. The reader maps these directly onto
  `facesOffsets` and `faces`.

Either way the exposed shape in USD is the same — a flat `int[]` index array
plus a length-`numFaces+1` `int[]` offsets array. Consumers that expect
per-face vertex counts can compute them as `facesOffsets[i+1] - facesOffsets[i]`.

##### Property: `omni:sci:array:owner:value` and `omni:sci:array:neighbour:value`

Owner and neighbour arrays follow OpenFOAM's convention: internal faces
appear in both arrays; boundary faces appear only in `owner`. The length of
`neighbour` is therefore `numFaces - sum(nFaces over boundary patches)`.

`numCells` is resolved either from the `note` header on `owner` (the
`nCells:` token) or by scanning the two arrays for the maximum cell id plus
one. The value is not authored as an attribute in v1 but is used internally
to size uniform internal fields.

#### Metadata

`OmniSciArrayAPI::CreateDeviceAttr()` is always `"cpu"` in the initial
implementation. There is no GPU-side resident-array path today.

#### Composition

```
/<caseStem>/Volume    OmniSciDataset
                              apiSchemas = ["OmniSciOpenFoamPolyMeshAPI",
                                            "OmniSciArrayAPI:points",
                                            "OmniSciArrayAPI:faces",
                                            "OmniSciArrayAPI:facesOffsets",
                                            "OmniSciArrayAPI:owner",
                                            "OmniSciArrayAPI:neighbour",
                                            <OmniSciFieldAPI:* and OmniSciArrayAPI:* for each internal field>]
```

---

### Boundary Patch

Each entry of `constant/polyMesh/boundary` becomes a child `OmniSciDataset`
prim under `/<caseStem>/Boundaries/`.
`OmniSciOpenFoamBoundaryPatchAPI` is applied to record the original patch
name, patch type, and the `[startFace, startFace+nFaces)` face range that the
patch owns in the parent mesh.

**Stage path:** `/<caseStem>/Boundaries/<patchName>`

The USD prim name is `TfMakeValidIdentifier(<originalPatchName>)`; the
unsanitised name is preserved in `omni:foam:patch:name`.

#### Properties

| OpenFOAM | OpenUSD | Description |
|----------|---------|-------------|
| patch name | `omni:foam:patch:name` (uniform string) | Original patch name before sanitisation. |
| patch type | `omni:foam:patch:type` (uniform string) | OpenFOAM patch type token such as `wall`, `patch`, `symmetryPlane`, `empty`. |
| `startFace` | `omni:foam:patch:startFace` (uniform int) | Starting face index of this patch in the parent mesh face list. |
| `nFaces` | `omni:foam:patch:nFaces` (uniform int) | Number of faces belonging to this patch. |
| (implicit) parent mesh | `omni:foam:patch:mesh` (rel) | Relationship back to the owning `OmniSciDataset` volume prim. |

Unknown keys in a `boundary` entry (e.g. `inGroups`, `matchTolerance`,
`transform`) are parsed and skipped in v1; they are not surfaced as USD
attributes.

#### Composition

```
/<caseStem>/Boundaries/<patchName>  OmniSciDataset
                                            apiSchemas = ["OmniSciOpenFoamBoundaryPatchAPI"]
                                            omni:foam:patch:mesh → /<caseStem>/Volume
                                            omni:foam:patch:name      = "<originalName>"
                                            omni:foam:patch:type      = "<type>"
                                            omni:foam:patch:startFace = <int>
                                            omni:foam:patch:nFaces    = <int>
```

Boundary-face connectivity is intentionally not duplicated on the patch prim —
consumers slice `owner` / `faces` / `facesOffsets` from the parent volume
using the `startFace` and `nFaces` range.

---

### Internal Fields

Each file inside a numeric time directory is inspected for an `internalField`
entry. Supported forms are:

- `internalField uniform <scalar>` → replicated into a `float[nCells]` array.
- `internalField uniform (x y z)` → replicated into a `float3[nCells]` array.
- `internalField nonuniform List<scalar> ...` → `float[count]` array (lazy).
- `internalField nonuniform List<vector> ...` → `float3[count]` array (lazy).

Files that do not contain a recognisable `internalField` entry, or whose list
type is not `scalar` / `vector`, are skipped silently. Tensor-, symmTensor-,
and surface-field variants are recorded as capability gaps in Appendix D.

For each accepted field, the plugin authors on the volume prim:

- `OmniSciFieldAPI:<instance>` with `name = <sourceFilename>` and
  `association = "element"` (all internal fields are cell-centred in v1).
- `OmniSciArrayAPI:<instance>` with `device = "cpu"` and a lazy
  `omni:sci:array:<instance>:value` of the appropriate array type.

The `<instance>` USD identifier is `TfMakeValidIdentifier(sourceFilename)`;
the unsanitised source filename is preserved on `omni:sci:field:<instance>:name`.

##### Property: `omni:sci:field:<name>:association`

| OpenFOAM declaration | OpenUSD token |
|----------------------|---------------|
| `class volScalarField` / `class volVectorField` (cell-centred) | `"element"` |

No `"node"` or `"none"` associations are produced in v1; vertex-associated
OpenFOAM fields (`pointScalarField`, `pointVectorField`) are not read.

##### Field Inclusion Rule

A field is authored on the volume prim only if a file with the same name is
present in **every** scanned time directory. Fields that appear in only a
subset of time steps are skipped silently — this avoids time samples with
missing data that would otherwise propagate as NaN-on-`Get()` surprises into
consumers.

---

### Time-Varying Data

If one or more numeric directories are present at the case root, the plugin
treats the case as time-varying. Each numeric directory is recorded as a
time step in on-disk numeric order (e.g. `0`, `0.001`, `0.002` → steps
0, 1, 2).

The USD time ordinate written for each step is:

```
timeCode = ResolveSampleTime(step) * timeScale + timeOffset
```

and the result is interpreted as canonical **simulation seconds**. The
emitted layer sets `timeCodesPerSecond = 1.0` so `Usd.Stage.Open` plays back
at real-time; host pipelines that compose into a stage whose TCPS is not 1
are responsible for authoring an
`Sdf.LayerOffset(scale=stage.GetTimeCodesPerSecond(), 0)` on the payload /
sublayer / reference arc -- the plugin does not ship a helper API for this.

`ResolveSampleTime` is chosen by the `timeSource` format argument:

- `"TimeStep"` (default) — uses the sample index (0, 1, 2, …). Pick `timeScale`
  to be the per-step `dt` in seconds when real-time playback is desired.
- `"TimeValue"` — uses the numeric value parsed from the time-directory name
  (e.g. `0.001` → 0.001). Pick `timeScale = 1.0` when the OpenFOAM case stores
  time in seconds (the usual convention).

Fields are registered via `CaeFileFormatData::RegisterLazyTimeSamples()`.
A field present in only one time directory remains a one-sample attribute at
its transformed source time; it does not author a default value.

#### Properties

| OpenFOAM | OpenUSD | Description |
|----------|---------|-------------|
| Numeric time directories | Number of authored time samples | One USD time code per present time directory. |
| Time-directory numeric value (e.g. `"0.001"`) | Time code after `timeScale`/`timeOffset` | Used when `timeSource = "TimeValue"`. |
| Field file presence across time dirs | Inclusion in lazy sampling | Missing-in-some-steps fields are skipped (see above). |

---

## Appendices

### Appendix A: File Format Arguments

The `OmniSciOpenFoamFileFormat` plugin accepts the following arguments when
opening a layer via `SdfLayer::FindOrOpen()` or `Usd.Stage.Open()`.

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `mountPath` | string | filename-stem root | Absolute prim path used for sublayer placement. Flat layer argument only. |
| `timeSource` | string | `"TimeStep"` | Native time source: `"TimeStep"` for sample index, `"TimeValue"` for the numeric time-directory value. Also reachable via `OmniSciFileFormatArgsTimeAPI:source`. |
| `timeScale` | float | 1.0 | Multiplier that converts the chosen source into simulation seconds (per-step `dt` for `TimeStep`; `1.0` when the case stores time in seconds for `TimeValue`). Also reachable via `OmniSciFileFormatArgsTimeAPI:scale`. |
| `timeOffset` | float | 0.0 | Additive offset (simulation seconds) applied after `timeScale`. Also reachable via `OmniSciFileFormatArgsTimeAPI:offset`. |
| `cacheMode` | string | `"all"` | Lazy-array value retention policy: `"all"` caches sampled values; `"static"` and `"none"` do not retain sampled values. Also reachable via `OmniSciFileFormatArgsAPI:cacheMode`. |
| `ioThreads` | int | 1 | Number of worker threads used for chunked binary-array reads. Also reachable via `OmniSciFileFormatArgsStreamingAPI:ioThreads`. |

### Appendix B: Stage Layout Example

An OpenFOAM case `minimal.foam` with one cubic cell, one `walls` patch, and
internal fields `T` (scalar) and `U` (vector) sampled at time directories
`1/` and `2/` is intended to map to:

```text
/minimal                                 UsdGeomScope  (default prim, Z-up)
  /minimal/Volume                        OmniSciDataset
                                         apiSchemas = ["OmniSciOpenFoamPolyMeshAPI",
                                                       "OmniSciArrayAPI:points",
                                                       "OmniSciArrayAPI:faces",
                                                       "OmniSciArrayAPI:facesOffsets",
                                                       "OmniSciArrayAPI:owner",
                                                       "OmniSciArrayAPI:neighbour",
                                                       "OmniSciFieldAPI:T",
                                                       "OmniSciArrayAPI:T",
                                                       "OmniSciFieldAPI:U",
                                                       "OmniSciArrayAPI:U"]
                                         omni:sci:array:points:device        = "cpu"
                                         omni:sci:array:faces:device         = "cpu"
                                         omni:sci:array:facesOffsets:device  = "cpu"
                                         omni:sci:array:owner:device         = "cpu"
                                         omni:sci:array:neighbour:device     = "cpu"
                                         omni:sci:field:T:name               = "T"
                                         omni:sci:field:T:association        = "element"
                                         omni:sci:array:T:device             = "cpu"
                                         omni:sci:field:U:name               = "U"
                                         omni:sci:field:U:association        = "element"
                                         omni:sci:array:U:device             = "cpu"
                                         -- lazy (sublayer) --
                                         omni:sci:array:points:value        = float3[8]
                                         omni:sci:array:faces:value         = int[24]
                                         omni:sci:array:facesOffsets:value  = int[7] = [0,4,8,12,16,20,24]
                                         omni:sci:array:owner:value         = int[6]  = [0,0,0,0,0,0]
                                         omni:sci:array:neighbour:value     = int[0]
                                         omni:sci:array:T:value @ t=0       = float[1]   = [300.0]
                                         omni:sci:array:T:value @ t=1       = float[1]   = [350.0]
                                         omni:sci:array:U:value @ t=0       = float3[1]  = [(1,0,0)]
                                         omni:sci:array:U:value @ t=1       = float3[1]  = [(0,1,0)]

    /minimal/Boundaries          UsdGeomScope
      /minimal/Boundaries/walls  OmniSciDataset
                                         apiSchemas = ["OmniSciOpenFoamBoundaryPatchAPI"]
                                         omni:foam:patch:mesh → /minimal/Volume
                                         omni:foam:patch:name      = "walls"
                                         omni:foam:patch:type      = "wall"
                                         omni:foam:patch:startFace = 0
                                         omni:foam:patch:nFaces    = 6
```

### Appendix C: Capability Gaps

The following OpenFOAM concepts have no current OpenUSD representation in
this plugin. They are recorded here to guide future work.

| OpenFOAM Concept | Gap Description | Notes |
|------------------|-----------------|-------|
| Boundary-field values in field files | Only `internalField` is read; the per-patch `boundaryField` dictionary is skipped. | Could be surfaced as `OmniSciArrayAPI:<name>_<patch>` on the patch prim, or as extra lazy arrays on the boundary patch. |
| Tensor / symmTensor / sphericalTensor fields | Only scalar and vector internal fields are read. | Would require additional `OmniSciArrayAPI` value types (e.g. `Matrix3d[]`, `float9[]`) and mapping the element-layout convention. |
| `surfaceScalarField` / `surfaceVectorField` | Face-centred fields are not read. | Should map to `association = "face"` once a canonical token is agreed. |
| `pointScalarField` / `pointVectorField` | Point-centred fields are not read. | Would use `association = "node"` on the volume prim. |
| `pointZones` / `faceZones` / `cellZones` | Named zone groupings in `constant/polyMesh/*Zones` are not read. | Could map to `UsdGeomSubset` prims or dedicated API schemas. |
| `processor*/polyMesh/` (parallel decomposition) | Decomposed meshes are not reassembled by the plugin. | Users must run `reconstructPar` before opening; future work may read the decomposition directly. |
| Dynamic / moving mesh | `pointMotionU`, `cellMotionU`, dynamic-mesh dictionaries are not consumed. | Would map to time-sampled `omni:sci:array:points:value`. |
| `lagrangian/` particle data | Particle fields are not read. | Could map to `UsdGeomPoints` or an `OmniSciDataset` with `OmniSciCaePointCloudAPI`. |
| Compressed files (`.gz`) | Plugin opens files via `std::ifstream`; no gzip layer. | Users must decompress with `foamDecompress` (or similar) before opening. |
| `faceCompactList` ASCII form | Only binary `faceCompactList` is supported. | ASCII variant is rare in practice; add on demand. |
| `uniform` as true constant | Uniform internal fields are materialised as a dense `float[nCells]` array. | A future version could expose a scalar attribute alongside a shape hint rather than replicating. |
| Physical-dimensionality metadata (`[0 1 -1 0 0 0 0]`) | Field dimensions are not preserved. | Could be stored in `customData` on `OmniSciFieldAPI` instances. |
| `controlDict` / `fvSchemes` / `fvSolution` | Solver metadata is not read. | Out of scope for geometry-focused v1; could later be exposed as prim `customData`. |
