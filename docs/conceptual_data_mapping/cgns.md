<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CGNS and OpenUSD Conceptual Data Mapping

Resolver-backed identifiers are supported for self-contained CGNS files.
External HDF5 links and other implicit sidecars are not resolver-backed because
the CGNS library does not expose their names to the plugin for resolution.

**Document Version:** 1.0.0
**Last Update:** 2026-04-13

## Introduction

### Overview

This document describes how CFD General Notation System (CGNS) data structures are
translated into OpenUSD prims and properties by the `OmniSciCgnsFileFormat` plugin.  It
serves as the authoritative reference for developers working on the plugin or
consuming the USD representation of CGNS data.

The mapping is **one-way (CGNS → OpenUSD)**.  The plugin is read-only; round-trip
write support is not currently implemented.  The document notes where CGNS concepts
have no OpenUSD representation (capability gaps) and where OpenUSD has no CGNS
equivalent.

CGNS organises data in a tree of typed nodes described by the Standard Interface Data
Structures (SIDS).  OpenUSD organises data in a scenegraph of prims with typed schemas
and composable API schemas.  The primary challenge in the mapping is that CGNS is a
hierarchical scientific data format with rich mesh topology and field data concepts,
while USD is a general scene-description format whose geometry schemas were designed
for rendering rather than simulation.  The mapping bridges this gap through two custom
schema plugins:

- **OmniSci** (`omni:sci:` namespace) — format-agnostic base schemas for scientific
  datasets, fields, and arrays.
- **OmniSciCgns** (`omni:cgns:` namespace) — CGNS-specific API schemas applied on top of
  the OmniSci base types.

Large array data (coordinates, connectivity, field values) is not loaded
eagerly. One `CaeFileFormatData` backend stores structure and defers sampled
array reads until an explicit-time `UsdAttribute::Get()` is called.

### References

This document has been prepared in reference to the software and specification versions
listed below.

#### CGNS Reference

| Version | Reference Documents |
|---------|---------------------|
| 4.4 | [CGNS Standard Interface Data Structures (SIDS)](https://cgns.org/standard/SIDS/CGNS_SIDS.html), [CGNS Mid-Level Library (MLL)](https://cgns.org/standard/MLL/CGNS_MLL.html), [CGNS GitHub Repository](https://github.com/CGNS/CGNS) |

#### OpenUSD Reference

| Version | Reference Documents |
|---------|---------------------|
| 24.08 | [OpenUSD C++ and Schema Documentation](https://openusd.org/release/api/index.html), [OpenUSD GitHub Repository](https://github.com/PixarAnimationStudios/OpenUSD), [USD Terms and Concepts](https://openusd.org/release/glossary.html) |

### General Assumptions and Constraints

**One-way mapping (CGNS → OpenUSD).** The plugin provides read-only access; writing
OpenUSD back to CGNS is not supported.  Therefore this document documents only the
CGNS → OpenUSD direction.

**Only 3-D unstructured zones are read.** CGNS bases with `CellDimension ≠ 3` are
silently skipped.  Zones with `ZoneType = Structured` are skipped.  Polyhedral element
types (`NGON_n`, `NFACE_n`) and mixed sections (`MIXED`) are supported for
unstructured zones.

**Lazy array loading.** Coordinate, connectivity, and field arrays are registered as
lazy attributes; data is fetched from the CGNS file on first access.  The
`omni:sci:array:<name>:value` attribute on a prim is intentionally absent from
`GetAuthoredProperties()` but resolvable by value resolution.  This is an intentional
implementation detail that consumers must account for.

**Schema composition over inheritance.** Format-specific attributes are
added to prims via API schemas, not by subclassing a base prim type.  Multiple API
schemas can coexist on the same prim (e.g., `OmniSciCgnsGridCoordinatesAPI` + multiple
`OmniSciArrayAPI` instances on a grid-coordinate prim).

**CGNS file format arguments.** Several aspects of the mapping can be tuned
via file-format arguments passed when opening the layer: `baseName`,
`zoneName` (filtering), `intSize`, `floatSize` (numeric precision),
`timeScale`, `timeOffset`, `timeSource` (time-varying data).

**Up-axis.** All stages produced by this plugin set `UsdGeomSetStageUpAxis(Z)` to
match the CGNS convention (Z-up for most CFD solvers).

**Prim name sanitisation.** All CGNS node names are passed through
`TfMakeValidIdentifier()` before use as prim names.

### Definitions, Acronyms, Abbreviations

| Term or Abbreviation | Description |
|----------------------|-------------|
| CGNS | CFD General Notation System — an ISO standard data model and file format for CFD and related simulation data. |
| SIDS | Standard Interface Data Structures — the CGNS specification that defines node types and their semantics. |
| CGNSBase_t | Top-level CGNS container node; records mesh dimensionality and groups zones. |
| Zone_t | A CGNS zone: a single mesh region with coordinates, connectivity sections, flow solutions, and boundary conditions. |
| ZoneType_t | Enumeration distinguishing `Structured` and `Unstructured` zone layouts. |
| GridCoordinates_t | CGNS node holding vertex coordinate arrays for a zone. |
| Elements_t | CGNS node describing one connectivity section (element type + range + connectivity array) in an unstructured zone. |
| ElementType_t | CGNS enumeration of element topologies (NODE, BAR_2, TRI_3, QUAD_4, TETRA_4, PYRA_5, PENTA_6, HEXA_8, MIXED, NGON_n, NFACE_n, and higher-order variants). |
| FlowSolution_t | CGNS node holding one set of field data (scalars, vectors) at a given grid location within a zone. |
| GridLocation_t | CGNS enumeration specifying where field values are located: Vertex, CellCenter, FaceCenter, EdgeCenter, etc. |
| ZoneBC_t / BC_t | CGNS nodes describing boundary condition patches on a zone. |
| ZoneGridConnectivity_t | CGNS nodes describing inter-zone connectivity (matching interfaces, overset patches). |
| Family_t | CGNS grouping mechanism that associates zones or boundaries with CAD families. |
| DataArray_t | CGNS node holding a named numerical array (coordinates, field values). |
| MLL | Mid-Level Library — the C/Fortran API for reading and writing CGNS files (`libcgns`). |
| OmniSci | Format-agnostic USD schema plugin (`omni:sci:` namespace) providing base types for scientific datasets. |
| OmniSciCgns | CGNS-specific USD schema plugin (`omni:cgns:` namespace) providing API schemas for CGNS concepts. |
| API schema | A non-typed USD schema that adds a reusable set of properties to any prim via `apiSchemas`. |
| Lazy attribute | An attribute whose value is not stored in the primary layer but is resolved on demand from a `CaeFileFormatData` sublayer. |

---

## Concepts

The table below lists all top-level CGNS SIDS node types relevant to a 3-D
unstructured CFD dataset, with their OpenUSD equivalents.  Where no mapping is
implemented, the OpenUSD column is left blank and the description notes the gap.

| CGNS | OpenUSD | Description |
|------|---------|-------------|
| [CGNSBase_t](#cgnsbase_t) | Untyped prim (type token `"CGNSBase"`) | Top-level container; child of the root Scope. Groups zones; dimensions are not currently authored. |
| [Zone_t (Unstructured)](#zone_t-unstructured) | Prim (type token `"CGNSZone"`) + `OmniSciCgnsZoneAPI` | Zone prim; owns relationships to grid-coordinate, section, and flow-solution prims. |
| Zone_t (Structured) | | **Not implemented.** Structured zones are skipped by the reader. |
| [GridCoordinates_t](#gridcoordinates_t) | Prim (type token `"CGNSGridCoordinates"`) + `OmniSciCgnsGridCoordinatesAPI` + `OmniSciArrayAPI:gridCoordinatesX/Y/Z` | Coordinate-node prim with lazy X/Y/Z coordinate arrays. |
| [Elements_t](#elements_t) | `OmniSciDataset` + `OmniSciCgnsUnstructuredElementsAPI` | One prim per CGNS section; carries element type, range, and lazy connectivity/offset arrays. |
| [FlowSolution_t](#flowsolution_t) | Prim (type token `"CGNSFlowSolution"`) + `OmniSciCgnsFlowSolutionAPI` + `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` | Flow solution prim with per-field metadata and lazy value arrays; supports time-varying data. |
| ZoneBC_t / BC_t | | **Not implemented.** Boundary condition data is not read. |
| ZoneGridConnectivity_t | | **Not implemented.** Multi-zone interface connectivity is not read. |
| Family_t | | **Not implemented.** Family groupings are not read. |
| ParticleZone_t | | **Not implemented.** Particle data is not read. |
| DiscreteData_t | | **Not implemented.** Similar structure to FlowSolution_t; not yet read. |
| ReferenceState_t | | **Not implemented.** Reference state (freestream conditions) is not read. |
| [BaseIterativeData_t / ZoneIterativeData_t](#time-varying-data) | USD time samples on `omni:sci:array:<name>:value` | Time step / iteration index arrays drive which FlowSolution node is loaded per time sample. |

### CGNSBase_t

A `CGNSBase_t` node is the top-level container in a CGNS file.  It records the
topological cell dimension and physical space dimension of all zones it contains, and
groups zones under a common name.

In OpenUSD the base is represented as an untyped prim whose type token string is
`"CGNSBase"` (an informal type name — not a registered USD schema).  It is a child of
the root `UsdGeomScope` prim.

**Stage path:** `/<filenameStem>/<baseName>`

#### Properties

| CGNS | OpenUSD | Description |
|------|---------|-------------|
| `CGNSBase_t` node name | Prim name (`TfMakeValidIdentifier`) | CGNS base name becomes the prim name. |
| `CellDimension` | *(not yet authored)* | Cell topological dimension (2 or 3). Gap: not currently stored as a prim attribute. |
| `PhysicalDimension` | *(not yet authored)* | Physical space dimension. Gap: not currently stored. |
| `Zone_t` children | Child prims of type `"CGNSZone"` | Each unstructured zone becomes a child prim under the base prim. |

#### Metadata

`CellDimension` and `PhysicalDimension` are not currently preserved in the USD layer.
They could be added as `custom int` attributes or as prim `customData` entries in a
future revision.

#### Composition

The base prim is defined with `ctx.stage->DefinePrim(path, TfToken("CGNSBase"))`.
Because `"CGNSBase"` is not a registered schema type, the prim has no generated C++
class; it is accessed as a plain `UsdPrim`.

---

### Zone_t (Unstructured)

A `Zone_t` node groups all mesh data for one zone: vertex coordinates, element
connectivity sections, flow solutions, and boundary conditions.

In OpenUSD an unstructured zone becomes a prim typed `"CGNSZone"` (informal type
token, no registered C++ class) with the `OmniSciCgnsZoneAPI` schema applied.
The `"CGNSZone"` type marks the prim as a CGNS-domain container; `OmniSciCgnsZoneAPI`
adds the CGNS-specific relationships.  The zone prim deliberately does *not* carry
the `OmniSciDataset` type — the arrays live on the section and grid-coordinate
prims, not on the zone itself.

**Stage path:** `/<filenameStem>/<baseName>/<zoneName>`

#### Properties

| CGNS | OpenUSD | Description |
|------|---------|-------------|
| `Zone_t` node name | Prim name | Zone name becomes the prim name (sanitised). |
| `ZoneType = Unstructured` | Prim type `"CGNSZone"` | Structured zones are skipped; unstructured zones become `"CGNSZone"`-typed prims. |
| `VertexSize` | *(not authored)* | Total vertex count; not stored as an attribute. |
| `CellSize` | *(not authored)* | Total cell count; not stored as an attribute. |
| `GridCoordinates_t` children | `omni:cgns:zone:gridCoordinates` relationship targets | Relationships point to the grid-coordinate prims. |
| `Elements_t` children | `omni:cgns:zone:sections` relationship targets | Relationships from `OmniSciCgnsZoneAPI` point to the section prims. |
| `FlowSolution_t` children | `omni:cgns:zone:flowSolutions` relationship targets | Relationships point to the flow-solution prims. |

##### Property: `omni:cgns:zone:gridCoordinates`

Relationship authored on the zone prim by `OmniSciCgnsZoneAPI::CreateGridCoordinatesRel()`.
Targets are the grid-coordinate prims under the zone.

| Format | Name | Type |
|--------|------|------|
| CGNS | `GridCoordinates_t` children (implicit) | CGNS tree hierarchy |
| OpenUSD | `omni:cgns:zone:gridCoordinates` | `rel` (relationship) |

##### Property: `omni:cgns:zone:sections`

Relationship authored on the zone prim by `OmniSciCgnsZoneAPI::CreateSectionsRel()`.
Targets are the `OmniSciDataset` prims representing `Elements_t` sections.

| Format | Name | Type |
|--------|------|------|
| CGNS | `Elements_t` children (implicit) | CGNS tree hierarchy |
| OpenUSD | `omni:cgns:zone:sections` | `rel` (relationship) |

##### Property: `omni:cgns:zone:flowSolutions`

Relationship authored by `OmniSciCgnsZoneAPI::CreateFlowSolutionsRel()`.
Targets are the flow-solution prims under the zone.

| Format | Name | Type |
|--------|------|------|
| CGNS | `FlowSolution_t` children (implicit) | CGNS tree hierarchy |
| OpenUSD | `omni:cgns:zone:flowSolutions` | `rel` (relationship) |

#### Metadata

`VertexSize` and `CellSize` (the `cgsize_t[9]` array from `cg_zone_read`) are not
currently authored as USD attributes.  They are available at read time and could be
added as `custom int` attributes in a future revision.

#### Composition

The zone prim is defined with `ctx.stage->DefinePrim(path, OmniSciCgnsFileFormatTokens->CGNSZone)`
and then `OmniSciCgnsZoneAPI::Apply()` is called on the resulting prim.  Grid
coordinates, sections, and flow solutions are authored as child prims and wired back
to the zone through relationships.

---

### GridCoordinates_t

A `GridCoordinates_t` node in CGNS contains one or more `DataArray_t` children
(typically `CoordinateX`, `CoordinateY`, `CoordinateZ`) holding vertex positions.

In OpenUSD each `GridCoordinates_t` node becomes a prim typed
`"CGNSGridCoordinates"` (informal type token) with `OmniSciCgnsGridCoordinatesAPI`
applied.  Coordinate arrays are represented as `OmniSciArrayAPI` instances on that
grid-coordinate prim.  The array values are **not** stored as primvars on a
`UsdGeomMesh` because the plugin does not construct a `UsdGeomMesh` prim; instead
the raw coordinate data is accessible via lazy attributes for downstream processing.

**Stage path:** `/<filenameStem>/<baseName>/<zoneName>/<gridCoordinatesName>`

#### Properties

| CGNS | OpenUSD | Description |
|------|---------|-------------|
| `GridCoordinates_t` node name | Prim name | Sanitised to valid USD identifier. |
| `CoordinateX` DataArray_t | `OmniSciArrayAPI:gridCoordinatesX` instance + lazy attr `omni:sci:array:gridCoordinatesX:value` | X-component of vertex coordinates. Array dtype matches file precision (float or double), controllable via `floatSize` argument. |
| `CoordinateY` DataArray_t | `OmniSciArrayAPI:gridCoordinatesY` instance + lazy attr `omni:sci:array:gridCoordinatesY:value` | Y-component. |
| `CoordinateZ` DataArray_t | `OmniSciArrayAPI:gridCoordinatesZ` instance + lazy attr `omni:sci:array:gridCoordinatesZ:value` | Z-component. |
| `Rind` (ghost nodes) | *(not read)* | Rind/ghost point data is not loaded. |

##### Property: `omni:sci:array:gridCoordinatesX:value`

| Name | Data Type |
|------|-----------|
| CGNS `CoordinateX` | `RealSingle` (float32) or `RealDouble` (float64) |
| OpenUSD `omni:sci:array:gridCoordinatesX:value` | `float[]` or `double[]` |

Array length normally equals `VertexSize` for unstructured zones.

#### Metadata

`OmniSciArrayAPI::CreateDeviceAttr()` is set to `"cpu"` for all coordinate arrays,
indicating host memory residency.

#### Composition

The lazy attribute is registered in the `CaeFileFormatData` sublayer; the primary layer
carries only the `OmniSciArrayAPI` descriptor (the `device` attribute).  Accessing
`UsdAttribute::Get()` on `omni:sci:array:gridCoordinatesX:value` triggers the
lazy-load callback which reopens the CGNS file under a process-wide mutex.

---

### Elements_t

A CGNS `Elements_t` node describes one section of unstructured element connectivity.
All elements in a section have the same type (or `MIXED` / `NGON_n` / `NFACE_n` for
heterogeneous sections).  Each section carries a contiguous index range within the
zone.

In OpenUSD each `Elements_t` node becomes a child `OmniSciDataset` prim with
`OmniSciCgnsUnstructuredElementsAPI` applied and lazy connectivity arrays registered.

**Stage path:** `/<filenameStem>/<baseName>/<zoneName>/<sectionName>`

#### Properties

| CGNS | OpenUSD | Description |
|------|---------|-------------|
| `Elements_t` node name | Prim name | Section name, sanitised to a valid USD identifier. |
| `ElementType_t` enum | `omni:cgns:unstructured_elements:elementType` (token) | One of the CGNS element type tokens (e.g., `"HEXA_8"`, `"NGON_n"`, `"MIXED"`). Full enum list in [Appendix A](#appendix-a-supported-elementtype_t-tokens). |
| `ElementRange[2]` | `omni:cgns:unstructured_elements:elementRange` (int2) | `[firstElement, lastElement]`, 1-based, global within zone. |
| `ElementSizeBoundary` | `omni:cgns:unstructured_elements:elementSizeBoundary` (int) | Count of boundary elements at the start of the connectivity array. `0` if no split. |
| `ElementConnectivity` DataArray_t | `OmniSciArrayAPI:elementConnectivity` + lazy attr `omni:sci:array:elementConnectivity:value` | Flat node-index connectivity array. 1-based CGNS indices. |
| `ElementStartOffset` DataArray_t | `OmniSciArrayAPI:elementStartOffset` + lazy attr `omni:sci:array:elementStartOffset:value` | Start-offset array for `NGON_n`, `NFACE_n`, and `MIXED` sections only. |
| Back-reference to zone | `omni:cgns:unstructured_elements:zone` (rel) | Relationship to the parent zone prim for reverse traversal. |

##### Property: `omni:cgns:unstructured_elements:elementType`

| Name | Data Type |
|------|-----------|
| CGNS `ElementType_t` enum | C enum (`CGNS_ENUMT(ElementType_t)`) |
| OpenUSD `omni:cgns:unstructured_elements:elementType` | `token` (string value matching enum name, e.g., `"HEXA_8"`) |

##### Property: `omni:sci:array:elementConnectivity:value`

| Name | Data Type |
|------|-----------|
| CGNS `ElementConnectivity` | 1-based integer array (32-bit or 64-bit) |
| OpenUSD `omni:sci:array:elementConnectivity:value` | `int[]` or `int64[]` (controlled by `intSize` argument) |

#### Metadata

`OmniSciArrayAPI::CreateDeviceAttr()` is `"cpu"` for both connectivity arrays.

#### Composition

The `ElementStartOffset` lazy attribute is only registered for polyhedral sections
(`NGON_n`, `NFACE_n`, `MIXED`).  For fixed-topology sections (e.g., `HEXA_8`) no
offset array is needed and no `OmniSciArrayAPI:elementStartOffset` instance is applied.

---

### FlowSolution_t

A CGNS `FlowSolution_t` node holds one complete set of field data for a zone at a
specific grid location (vertex-centred or cell-centred).  It contains one or more
`DataArray_t` children, each holding a named field (e.g., `Pressure`, `VelocityX`).

In OpenUSD each `FlowSolution_t` becomes a prim typed `"CGNSFlowSolution"` (informal
type token) with `OmniSciCgnsFlowSolutionAPI` applied to carry the `GridLocation`, and
one `OmniSciFieldAPI:<fieldName>` + `OmniSciArrayAPI:<fieldName>` pair per field.

**Stage path:** `/<filenameStem>/<baseName>/<zoneName>/<flowSolutionName>`

#### Properties

| CGNS | OpenUSD | Description |
|------|---------|-------------|
| `FlowSolution_t` node name | Prim name | Sanitised to valid USD identifier. |
| `GridLocation_t` enum | `omni:cgns:flow_solution:gridLocation` (token) | One of `"Vertex"`, `"CellCenter"`, `"FaceCenter"`, `"EdgeCenter"`, `"IFaceCenter"`, `"JFaceCenter"`, `"KFaceCenter"`. |
| `DataArray_t` child (field) | `OmniSciFieldAPI:<fieldName>` + `OmniSciArrayAPI:<fieldName>` | One pair per field. Instance name is `TfMakeValidIdentifier(cgnsFieldName)`. |
| Field name (original) | `omni:sci:field:<name>:name` (string) | Original CGNS field name (before identifier sanitisation). |
| Field association | `omni:sci:field:<name>:association` (token) | `"node"` for `GridLocation = Vertex`, `"element"` for `GridLocation = CellCenter`. |
| Field data array | lazy attr `omni:sci:array:<name>:value` | Float, double, int, or int64 array of length `VertexSize` or `CellSize`. |

##### Property: `omni:cgns:flow_solution:gridLocation`

| Name | Data Type |
|------|-----------|
| CGNS `GridLocation_t` | C enum |
| OpenUSD `omni:cgns:flow_solution:gridLocation` | `token` |

Allowed tokens and CGNS enum mapping:

| CGNS `GridLocation_t` | OpenUSD token | `omni:sci:field:*:association` |
|-----------------------|---------------|-------------------------------|
| `Vertex` | `"Vertex"` | `"node"` |
| `CellCenter` | `"CellCenter"` | `"element"` |
| `FaceCenter` | `"FaceCenter"` | `"element"` |
| `EdgeCenter` | `"EdgeCenter"` | `"element"` |
| `IFaceCenter` | `"IFaceCenter"` | `"element"` |
| `JFaceCenter` | `"JFaceCenter"` | `"element"` |
| `KFaceCenter` | `"KFaceCenter"` | `"element"` |

##### Property: `omni:sci:array:<name>:value`

| Name | Data Type |
|------|-----------|
| CGNS `DataArray_t` (field) | `RealSingle`, `RealDouble`, `Integer`, or `LongInteger` |
| OpenUSD `omni:sci:array:<name>:value` | `float[]`, `double[]`, `int[]`, or `int64[]` |

Array length: `VertexSize` for vertex-located fields, `CellSize` for cell-centred
fields.

#### Metadata

`OmniSciArrayAPI::CreateDeviceAttr()` is `"cpu"` for all field arrays.

#### Composition

**Time-varying fields.** If `BaseIterativeData_t` / `ZoneIterativeData_t` nodes are
present in the CGNS file and reference multiple `FlowSolution_t` nodes by name, the
plugin identifies a "group leader" flow solution.  Field arrays on the group-leader
prim are registered as time samples (one sample per step) via
`CaeFileFormatData::RegisterLazyTimeSamples()`.  Non-leader members of the temporal group
are skipped.  Time sample values default to step indices unless `BaseIterativeData_t`
carries `TimeValues` or `IterationValues` arrays, which are used as the native
input to the time-code computation. The `timeScale` and `timeOffset` file-format
arguments shift and scale the chosen source into canonical **simulation seconds**:
`timecode = source_value * timeScale + timeOffset`. The emitted layer also sets
`timeCodesPerSecond = 1.0` so `Usd.Stage.Open` plays back at real-time. Host
pipelines that compose a CGNS layer into a stage whose `timeCodesPerSecond` is
not 1 are responsible for authoring the matching `Sdf.LayerOffset` on the
payload / sublayer / reference arc -- the plugin does not ship a helper API
for this. The one-line convention:

```python
prim.GetPayloads().AddPayload(
    Sdf.Payload(
        assetPath="case.cgns",
        layerOffset=Sdf.LayerOffset(stage.GetTimeCodesPerSecond(), 0)))
```

Without the offset, host playback is fast or slow by the ratio of host TCPS
to 1.

---

### Time-Varying Data

| CGNS | OpenUSD | Description |
|------|---------|-------------|
| `BaseIterativeData_t` with `TimeValues` | USD time sample ordinates (canonical simulation seconds) | Physical time values become USD time codes after `timeScale` and `timeOffset` are applied. |
| `BaseIterativeData_t` with `IterationValues` | USD time sample ordinates (sim seconds when `timeScale` encodes the per-iteration duration) | Iteration counts are not physical seconds by construction; pick `timeScale` to be the average iteration duration in seconds if real-time playback is desired. |
| `ZoneIterativeData_t` `FlowSolutionPointers` | Field array time samples | Maps each time step to a `FlowSolution_t` node; the group-leader prim accumulates one time sample per step. |

---

## Appendices

### Appendix A: Supported ElementType_t Tokens

The following `ElementType_t` values are recognised and stored verbatim as the
`omni:cgns:unstructured_elements:elementType` token.  Values not in this list result
in the default `"ElementTypeNull"` token.

| Linear Elements | Higher-Order Elements | Special |
|----------------|----------------------|---------|
| `NODE` | `BAR_3`, `BAR_4`, `BAR_5` | `MIXED` |
| `BAR_2` | `TRI_6`, `TRI_9`, `TRI_10`, `TRI_12`, `TRI_15` | `NGON_n` |
| `TRI_3` | `QUAD_8`, `QUAD_9`, `QUAD_12`, `QUAD_16`, `QUAD_P4_16`, `QUAD_25` | `NFACE_n` |
| `QUAD_4` | `TETRA_10`, `TETRA_16`, `TETRA_20`, `TETRA_22`, `TETRA_34`, `TETRA_35` | |
| `TETRA_4` | `PYRA_13`, `PYRA_14`, `PYRA_21`, `PYRA_29`, `PYRA_30`, `PYRA_P4_29`, `PYRA_50`, `PYRA_55` | |
| `PYRA_5` | `PENTA_15`, `PENTA_18`, `PENTA_24`, `PENTA_33`, `PENTA_38`, `PENTA_40`, `PENTA_66`, `PENTA_75` | |
| `PENTA_6` | `HEXA_20`, `HEXA_27`, `HEXA_32`, `HEXA_44`, `HEXA_56`, `HEXA_64`, `HEXA_98`, `HEXA_125` | |
| `HEXA_8` | | |

### Appendix B: File Format Arguments

The `OmniSciCgnsFileFormat` plugin accepts the following arguments when opening a layer
via `SdfLayer::FindOrOpen()` or `Usd.Stage.Open()`.

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `mountPath` | string | filename-stem root | Absolute prim path used for sublayer placement. Flat layer argument only. |
| `cacheMode` | string | `"all"` | Lazy-array retention: `all`, `static`, or `none`. Also reachable via `OmniSciFileFormatArgsAPI:cacheMode`. |
| `baseName` | string | (none) | If set, only the CGNS base with this name is loaded; others are skipped. Also reachable via `OmniSciFileFormatArgsCgnsAPI:baseName`. |
| `zoneName` | string | (none) | If set, only zones with this name are loaded within each loaded base. Also reachable via `OmniSciFileFormatArgsCgnsAPI:zoneName`. |
| `intSize` | int (32 or 64) | 0 (file default) | Override integer precision for connectivity arrays. Also reachable via `OmniSciFileFormatArgsCgnsAPI:intSize`. |
| `floatSize` | int (32 or 64) | 0 (file default) | Override floating-point precision for coordinate and field arrays. Also reachable via `OmniSciFileFormatArgsCgnsAPI:floatSize`. |
| `timeScale` | float | 1.0 | Multiplier applied to the chosen source value to land in simulation seconds. Use `1.0` when `timeSource = TimeValue` and the file already stores seconds; use the per-step `dt` in seconds when `timeSource = TimeStep`; pick the average iteration duration when `timeSource = IterationValue`. Also reachable via `OmniSciFileFormatArgsTimeAPI:scale`. |
| `timeOffset` | float | 0.0 | Additive offset (simulation seconds) applied after `timeScale`. Also reachable via `OmniSciFileFormatArgsTimeAPI:offset`. |
| `timeSource` | string | `"TimeStep"` | Native source: `"TimeStep"` uses zero-based sample index; `"TimeValue"` uses the `BaseIterativeData_t::TimeValues` array; `"IterationValue"` uses the `BaseIterativeData_t::IterationValues` counter (non-real-time by construction unless `timeScale` encodes the per-iteration duration). Also reachable via `OmniSciFileFormatArgsTimeAPI:source`. |

### Appendix C: Stage Layout Example

A CGNS file `mixer.cgns` with one base (`Base`), one unstructured zone (`Zone1`), two
sections (`InteriorCells`, `WallFaces`), and one flow solution (`FlowSolution`) maps
to:

```
/mixer                               UsdGeomScope  (default prim, Z-up)
  /mixer/Base                        Prim  type="CGNSBase"
    /mixer/Base/Zone1                Prim  type="CGNSZone"
                                       apiSchemas = ["OmniSciCgnsZoneAPI"]
                                       omni:cgns:zone:gridCoordinates → [/mixer/Base/Zone1/GridCoordinates]
                                       omni:cgns:zone:sections → [/mixer/Base/Zone1/InteriorCells,
                                                                   /mixer/Base/Zone1/WallFaces]
                                       omni:cgns:zone:flowSolutions → [/mixer/Base/Zone1/FlowSolution]

      /mixer/Base/Zone1/GridCoordinates
                                       Prim  type="CGNSGridCoordinates"
                                       apiSchemas = ["OmniSciCgnsGridCoordinatesAPI",
                                                     "OmniSciArrayAPI:gridCoordinatesX",
                                                     "OmniSciArrayAPI:gridCoordinatesY",
                                                     "OmniSciArrayAPI:gridCoordinatesZ"]
                                       omni:sci:array:gridCoordinatesX:device = "cpu"
                                       omni:sci:array:gridCoordinatesY:device = "cpu"
                                       omni:sci:array:gridCoordinatesZ:device = "cpu"
                                       -- lazy (sublayer) --
                                       omni:sci:array:gridCoordinatesX:value = float[nNodes]
                                       omni:sci:array:gridCoordinatesY:value = float[nNodes]
                                       omni:sci:array:gridCoordinatesZ:value = float[nNodes]

      /mixer/Base/Zone1/InteriorCells  OmniSciDataset
                                         apiSchemas = ["OmniSciCgnsUnstructuredElementsAPI",
                                                       "OmniSciArrayAPI:elementConnectivity"]
                                         omni:cgns:unstructured_elements:elementType    = "HEXA_8"
                                         omni:cgns:unstructured_elements:elementRange   = (1, 8000)
                                         omni:cgns:unstructured_elements:elementSizeBoundary = 0
                                         omni:cgns:unstructured_elements:zone → /mixer/Base/Zone1
                                         omni:sci:array:elementConnectivity:device = "cpu"
                                         -- lazy --
                                         omni:sci:array:elementConnectivity:value = int[64000]

      /mixer/Base/Zone1/WallFaces      OmniSciDataset
                                         apiSchemas = ["OmniSciCgnsUnstructuredElementsAPI",
                                                       "OmniSciArrayAPI:elementConnectivity",
                                                       "OmniSciArrayAPI:elementStartOffset"]
                                         omni:cgns:unstructured_elements:elementType    = "NGON_n"
                                         omni:cgns:unstructured_elements:elementRange   = (8001, 9200)
                                         omni:sci:array:elementConnectivity:device = "cpu"
                                         omni:sci:array:elementStartOffset:device  = "cpu"
                                         -- lazy --
                                         omni:sci:array:elementConnectivity:value  = int[...]
                                         omni:sci:array:elementStartOffset:value   = int[1201]

      /mixer/Base/Zone1/FlowSolution   Prim  type="CGNSFlowSolution"
                                         apiSchemas = ["OmniSciCgnsFlowSolutionAPI",
                                                       "OmniSciFieldAPI:Pressure",
                                                       "OmniSciArrayAPI:Pressure",
                                                       "OmniSciFieldAPI:VelocityX",
                                                       "OmniSciArrayAPI:VelocityX"]
                                         omni:cgns:flow_solution:gridLocation      = "CellCenter"
                                         omni:sci:field:Pressure:name              = "Pressure"
                                         omni:sci:field:Pressure:association       = "element"
                                         omni:sci:array:Pressure:device            = "cpu"
                                         omni:sci:field:VelocityX:name             = "VelocityX"
                                         omni:sci:field:VelocityX:association      = "element"
                                         omni:sci:array:VelocityX:device           = "cpu"
                                         -- lazy --
                                         omni:sci:array:Pressure:value             = float[nCells]
                                         omni:sci:array:VelocityX:value            = float[nCells]
```

### Appendix D: Capability Gaps

The following CGNS concepts have no current OpenUSD representation in this plugin.
They are recorded here to guide future work.

| CGNS Concept | Gap Description | Notes |
|--------------|-----------------|-------|
| `Zone_t (Structured)` | Structured zones are silently skipped by the reader. | Structured zones require index-space connectivity (IJK dimensions) which maps differently from unstructured connectivity. |
| `ZoneBC_t / BC_t` | Boundary condition data is not read. | BC patches could map to `UsdGeomSubset` prims with `OmniSciCgnsBC` API schema carrying `BCType_t`, Dirichlet/Neumann data sets, and `FamilyName`. |
| `ZoneGridConnectivity_t` | Multi-zone interface and overset connectivity is not read. | Matching interfaces could map to USD relationships between zone prims; overset may need custom API schemas. |
| `Family_t` | Family groupings are not read. | CGNS families group zones or boundaries with CAD surfaces; could map to USD `UsdGeomSubset` or a custom `OmniSciCgnsFamily` schema. |
| `CellDimension` / `PhysicalDimension` | Base dimensionality not stored. | Could be added as `custom int` attributes or prim `customData` on the `CGNSBase` prim. |
| `VertexSize` / `CellSize` | Zone size arrays not stored. | Useful for consumers that need element counts without loading connectivity arrays. |
| `DiscreteData_t` | Not read; has same structure as `FlowSolution_t`. | Could reuse the same prim pattern as `FlowSolution_t`. |
| `ReferenceState_t` | Freestream / reference conditions not read. | Could map to a global prim with scalar field attributes. |
| `ParticleZone_t` | Particle data not read. | Particle data could map to `UsdGeomPoints`. |
| `Rind` (ghost nodes/cells) | Ghost point/cell data in `GridCoordinates_t` is not loaded. | Rind data could be stored as additional lazy arrays with a `rind:` prefix. |
| `ElementSizeBoundary` authoring | The attribute is defined in the schema but currently always set to `0`. | Actual `ElementSizeBoundary` should be read from `cg_section_read` and stored. |
