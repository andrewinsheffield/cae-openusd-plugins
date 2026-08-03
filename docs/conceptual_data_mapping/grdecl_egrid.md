<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# GRDECL/EGRID/INIT/UNRST Reservoir Data and OpenUSD Conceptual Data Mapping

Resolver-backed EGRID, INIT, and UNRST files are supported independently.
GRDECL `INCLUDE` paths are resolved relative to the original deck asset, so
explicitly referenced multi-file decks are also supported.

**Document Version:** 0.3.0
**Last Update:** 2026-05-10

## Introduction

### Overview

This document describes how Eclipse-family reservoir corner-point grid and
cell-result data maps to OpenUSD prims and properties in this repository. It
is the conceptual companion to the `OmniSciReservoir` schema library and the
`omniSciEclipseFileFormat` plugin, which registers separate
`OmniSciGrdeclFileFormat`, `OmniSciEgridFileFormat`,
`OmniSciInitFileFormat`, and `OmniSciUnrstFileFormat` readers.

The implemented reader currently covers:

- GRDECL / Eclipse deck-style text grid descriptions (`.grdecl`, `.GRDECL`)
- Eclipse text decks (`.data`, `.DATA`) that include GRDECL-style grid/property
  files
- EGRID / Eclipse unformatted binary grid files (`.egrid`, `.EGRID`)
- INIT / Eclipse binary static-property files (`.init`, `.INIT`)
- UNRST / Eclipse unified restart files (`.unrst`, `.UNRST`)

GRDECL and EGRID are different encodings of the same reservoir-grid concept: a
logically Cartesian IJK grid whose geometry is described by corner-point pillar
data. OpenUSD represents the imported dataset as composable schemas:

- **OmniSci** (`omni:sci:` namespace) -- format-agnostic base schemas for
  scientific datasets, fields, and arrays.
- **OmniSciReservoir** (`omni:reservoir:` namespace) -- reservoir-specific
  APIs for native corner-point grids and cell-property packing metadata.

The mapping preserves the native corner-point representation. It does not
construct, author, or require a derived `UsdGeomMesh`, face-vertex mesh,
cell-center point cloud, or explicit polyhedral connectivity.

### References

This document has been prepared in reference to the public reservoir-grid
documentation and OpenUSD conventions listed below.

#### Reservoir Grid Reference

| Source | Reference Documents |
|--------|---------------------|
| Energistics RESQML 2.0.1 | [Eclipse GRDECL File as an IJK Grid](https://docs.energistics.org/RESQML/RESQML_TOPICS/RESQML-000-292-0-C-sv2010.html) |
| Open Porous Media | [OPM Flow reference documentation](https://opm-project.org/), including COORD/ZCORN/ACTNUM keyword descriptions |
| Open Porous Media | `grdecl` / `EclipseGrid` API documentation describing raw `dims`, `coord`, `zcorn`, `actnum`, and optional `mapaxes` data |

#### OpenUSD Reference

| Version | Reference Documents |
|---------|---------------------|
| 24.08 | [OpenUSD C++ and Schema Documentation](https://openusd.org/release/api/index.html), [OpenUSD GitHub Repository](https://github.com/PixarAnimationStudios/OpenUSD), [USD Terms and Concepts](https://openusd.org/release/glossary.html) |

### General Assumptions and Constraints

**One-way mapping (Eclipse-family files -> OpenUSD).** The implemented readers
are read-only. Writing OpenUSD back to GRDECL, EGRID, INIT, or UNRST is not in
scope.

**Native corner-point data only.** The schema preserves `COORD`, `ZCORN`, and
optional `ACTNUM` arrays plus lightweight interpretation metadata. It does not
define derived points, faces, cell connectivity, face normals, centroids,
volumes, transmissibility geometry, or rendering meshes.

**No derived USD mesh.** The GRDECL and EGRID readers do not generate a
`UsdGeomMesh` or apply `OmniSciCaeMeshAPI`.
Consumers that need an explicit visualization or simulation mesh can derive it
outside this schema from the raw corner-point arrays.

**Shared grid schema for GRDECL/EGRID.** GRDECL and EGRID use the same
`OmniSciReservoirCornerPointGridAPI` representation. INIT and UNRST are result
overlays: they author cell fields and reservoir packing metadata on the dataset
prim, but they do not author grid geometry.

**Single global grid in v1.** The implemented GRDECL and EGRID readers target
one global corner-point grid. Local grid refinements, host-grid relationships,
and multi-grid EGRID content are out of scope for this version.

**Limited deck evaluation.** For `.DATA` files, the reader follows `INCLUDE`
records and reads direct array keywords. It does not evaluate Eclipse edit
operations such as `COPY`, `EQUALS`, `ADD`, or `MULTIPLY`, and it does not
process faults, transmissibility multipliers, PVT data, solution data, summary
requests, or schedule/well data.

**Limited EGRID record evaluation.** For `.EGRID` files, the reader consumes the
global-grid records needed for native geometry: `GRIDHEAD`, `COORD`, `ZCORN`,
optional `ACTNUM`, and optional unit/MAPAXES metadata. It indexes but does not
interpret records after `ENDGRID`, including NNC, LGR, fault, transmissibility,
restart, or result-property records.

**Result overlays compose by prim path.** INIT and UNRST layers are intended to
overlay an existing GRDECL/EGRID grid layer at the same dataset prim path.
The default path is `/<filename-stem>`, with only the extension stripped, so
`CASE.EGRID`, `CASE.INIT`, and `CASE.UNRST` all default to `/CASE`.
For references or payloads, the host stage should add the grid and result files
to the same prim. No sibling file discovery or automatic composition is done.

**Stored result keywords only.** INIT and UNRST readers expose stored numeric
cell-sized keyword records. They do not derive convenience fields such as
`SOIL = 1 - SWAT - SGAS`; consumers such as DAV may compute those later if
needed.

**Lazy array loading.** Large arrays are registered as lazy
`omni:sci:array:<name>:value` attributes. The value may resolve through USD
value resolution without appearing in `GetAuthoredProperties()`.

**Schema composition over inheritance.** Reservoir-specific semantics are
expressed through API schemas applied to an `OmniSciDataset` prim. Generic
field metadata remains in `OmniSciFieldAPI`; reservoir-specific packing
metadata lives in `OmniSciReservoirCellPropertyAPI`.

**Indexing convention.** Logical-cell arrays use Eclipse-family order:
I cycles fastest, then J, then K. The `indexOrder` attribute records this as
`"eclipse"`.

**Activity convention.** `ACTNUM` is interpreted as zero for inactive cells and
nonzero for active cells. If `ACTNUM` is absent, all logical cells are active
unless future format-specific metadata says otherwise.

### Definitions, Acronyms, Abbreviations

| Term or Abbreviation | Description |
|----------------------|-------------|
| GRDECL | Text deck-style reservoir grid/property format used by Eclipse-family simulators and tools. |
| EGRID | Binary Eclipse-family grid file containing grid metadata and corner-point arrays. |
| INIT | Binary Eclipse-family static-property file, commonly containing active-cell packed initialization properties such as porosity, depth, pore volume, and region numbers. |
| UNRST | Binary Eclipse-family unified restart file containing time-varying stored reservoir solution keywords. |
| Corner-point grid | Logically Cartesian IJK grid whose cell corner depths are specified on vertical or near-vertical coordinate pillars. |
| IJK / logical cell | Integer cell address `(i, j, k)` in the logical grid dimensions `(NX, NY, NZ)`. |
| `COORD` | Pillar endpoint array. Each pillar is described by two 3-D points. |
| `ZCORN` | Cell-corner depth array. Each logical cell contributes eight corner depth values. |
| `ACTNUM` | Optional active-cell flag array over logical cells. |
| `MAPAXES` | Optional six-value map-axis transform used by Eclipse-family grids. |
| Active-cell packed | A property array layout with one value per active cell, rather than one value per logical cell. |
| Report step | Eclipse restart sequence number stored in `SEQNUM`. |
| OmniSci | Format-agnostic USD schema plugin (`omni:sci:` namespace) providing base types for scientific datasets. |
| OmniSciReservoir | Reservoir-specific USD schema plugin (`omni:reservoir:` namespace). |
| API schema | A non-typed USD schema that adds reusable properties to a prim through `apiSchemas`. |

---

## Concepts

The table below lists the Eclipse-family concepts covered by this mapping and
their OpenUSD equivalents.

| Source concept | OpenUSD | Description |
|----------------|---------|-------------|
| [Dataset root](#dataset-root) | `OmniSciDataset` at `/<filename-stem>` | The imported grid dataset and layer default prim. |
| [Grid dimensions](#grid-dimensions) | `omni:reservoir:grid:logicalCellDims` | Logical `(NX, NY, NZ)` dimensions. |
| [Source format](#source-format-metadata) | `omni:reservoir:grid:sourceFormat` | Provenance token: `grdecl`, `egrid`, or `unknown`. |
| [COORD](#corner-point-arrays) | `OmniSciArrayAPI:coord` | Raw pillar endpoint values. |
| [ZCORN](#corner-point-arrays) | `OmniSciArrayAPI:zcorn` | Raw cell-corner depth values. |
| [ACTNUM](#corner-point-arrays) | `OmniSciArrayAPI:actnum` | Optional raw active-cell flags. |
| Logical-to-active lookup | `OmniSciArrayAPI:logicalCellToActiveCell` | Optional derived logical-cell to packed active-cell index map; inactive cells are `-1`. |
| MAPAXES | `omni:reservoir:grid:mapAxes` | Optional six-value map-axis metadata. |
| GRIDUNIT / MAPUNITS / unit keywords | `omni:reservoir:grid:lengthUnit` | Optional source length unit string. |
| Cell property keyword | `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` + `OmniSciReservoirCellPropertyAPI:<name>` | Cell-centred property array plus reservoir packing metadata. |
| INIT stored property keyword | Same field/array/property API trio | Static result overlay. Arrays remain logical-cell or active-cell packed as stored. |
| UNRST stored property keyword | Same field/array/property API trio with time-sampled array values | Restart result overlay. USD time samples are in canonical simulation seconds; the file-format `TimeAPI` (`timeSource` / `timeScale` / `timeOffset`) selects the native concept and conversion. |
| Active-cell map (reserved) | `OmniSciArrayAPI:globalCellIndex` | Active-cell to logical-cell index map; reserved instance-name token but **not** authored by current readers. Derive from `logicalCellToActiveCell` or `ACTNUM` if needed. |
| Derived mesh / explicit topology | | **Out of scope.** Not authored by this schema or by the readers. |

### Dataset Root

Each reader anchors imported content at the layer default prim:

**Stage path:** `/<filename-stem>`

For GRDECL/EGRID, the root prim is an `OmniSciDataset` with
`OmniSciReservoirCornerPointGridAPI` applied. The same prim owns the raw grid
arrays and any cell-property arrays.

For INIT/UNRST, the root prim is also an `OmniSciDataset`, but the layer only
authors fields, array APIs, and `OmniSciReservoirCellPropertyAPI` metadata. It
does not apply `OmniSciReservoirCornerPointGridAPI`, because these files are
overlays on a grid supplied by a GRDECL/EGRID layer.

#### Composition

```text
/<filename-stem>      OmniSciDataset (default prim)
                        apiSchemas = [
                            "OmniSciReservoirCornerPointGridAPI",
                            "OmniSciArrayAPI:coord",
                            "OmniSciArrayAPI:zcorn",
                            "OmniSciArrayAPI:actnum",
                            <OmniSciFieldAPI:*>,
                            <OmniSciArrayAPI:*>,
                            <OmniSciReservoirCellPropertyAPI:*>
                        ]
```

No wrapper `Scope` and no derived mesh prim are authored for the v1 mapping.

### Grid Dimensions

The `logicalCellDims` attribute records the logical grid dimensions:

| Source | OpenUSD | Description |
|--------|---------|-------------|
| GRDECL `SPECGRID` / `DIMENS` | `omni:reservoir:grid:logicalCellDims` | `(NX, NY, NZ)` for the logical grid. |
| EGRID `GRIDHEAD` / equivalent metadata | `omni:reservoir:grid:logicalCellDims` | `(NX, NY, NZ)` decoded from the binary grid metadata. |

`logicalCellDims` determines the expected raw array sizes:

```text
coord length  = 6 * (NX + 1) * (NY + 1)
zcorn length  = 8 * NX * NY * NZ
actnum length = NX * NY * NZ, when present
```

### Source Format Metadata

The schema records source-file provenance through:

| Attribute | Type | Values |
|-----------|------|--------|
| `omni:reservoir:grid:sourceFormat` | `token` | `unknown`, `grdecl`, `egrid` |

This token does not change the interpretation of the raw grid arrays. It is
available for debugging, provenance, UI display, and reader-specific decisions.

Other lightweight metadata:

| Attribute | Type | Description |
|-----------|------|-------------|
| `omni:reservoir:grid:indexOrder` | `token` | Defaults to `eclipse`; I fastest, then J, then K. |
| `omni:reservoir:grid:depthDirection` | `token` | Defaults to `zDown`; raw arrays are not transformed. |
| `omni:reservoir:grid:name` | `string` | Original source grid name when available. |
| `omni:reservoir:grid:lengthUnit` | `string` | Source coordinate/depth length unit when known. |
| `omni:reservoir:grid:mapAxes` | `double[]` | Optional six-value MAPAXES-style metadata. |

### Corner-Point Arrays

Corner-point geometry is carried by `OmniSciArrayAPI` instances on the root
dataset prim. The GRDECL reader exposes the values lazily through
`omni:sci:array:<instance>:value`.

#### COORD

| Source | OpenUSD | Description |
|--------|---------|-------------|
| GRDECL/EGRID `COORD` | `OmniSciArrayAPI:coord` + lazy attr `omni:sci:array:coord:value` | Flat pillar endpoint values as `double[]`. |

`COORD` is stored in source-native order. Each pillar contributes six values:
top endpoint `(x1, y1, z1)` and bottom endpoint `(x2, y2, z2)`.

#### ZCORN

| Source | OpenUSD | Description |
|--------|---------|-------------|
| GRDECL/EGRID `ZCORN` | `OmniSciArrayAPI:zcorn` + lazy attr `omni:sci:array:zcorn:value` | Flat corner-depth values for all logical cells as `double[]`. |

`ZCORN` is stored in source-native Eclipse-family order. The schema does not
expand these values into explicit 3-D corner points.

#### ACTNUM

| Source | OpenUSD | Description |
|--------|---------|-------------|
| GRDECL/EGRID `ACTNUM` | `OmniSciArrayAPI:actnum` + lazy attr `omni:sci:array:actnum:value` | Optional logical-cell activity flags as `int[]`. |
| ACTNUM-derived lookup | `OmniSciArrayAPI:logicalCellToActiveCell` + lazy attr `omni:sci:array:logicalCellToActiveCell:value` | Optional logical-cell to active-cell packed index map as `int[]`; inactive cells are `-1`. |

If absent, all logical cells are considered active. If present, zero means
inactive and nonzero means active.

When `ACTNUM` is present, the GRDECL and EGRID readers also expose the
`logicalCellToActiveCell` lookup. This is an auxiliary index array for
active-cell-packed result overlays; it does not expand properties or author any
derived USD mesh.

### Cell Properties

Reservoir property keywords such as porosity, permeability, net-to-gross, and
region numbers are represented as generic fields plus reservoir-specific
packing metadata.

The implemented GRDECL reader exposes direct full-logical-cell arrays for the
following keyword families:

| Value type | Keywords |
|------------|----------|
| `double[]` | `PORO`, `PERMX`, `PERMY`, `PERMZ`, `NTG`, `SWATINIT`, `SWCR`, `SWL`, `SWU`, `SGL`, `SGCR`, `SGU`, `SOGCR`, `SOWCR`, `ISGCR` |
| `int[]` | `EQLNUM`, `FIPNUM`, `FLUXNUM`, `SATNUM`, `IMBNUM`, `PVTNUM`, `ROCKNUM` |

Arrays produced only through `COPY`, `EQUALS`, `ADD`, `MULTIPLY`, or other deck
edit operations are not materialized in this version.

Each property should apply:

- `OmniSciFieldAPI:<instance>`
- `OmniSciArrayAPI:<instance>`
- `OmniSciReservoirCellPropertyAPI:<instance>`

The `OmniSciFieldAPI` association must be `element`, because properties are
cell-centred over the reservoir grid.

#### Example

```usda
def OmniSciDataset "Model" (
    prepend apiSchemas = [
        "OmniSciReservoirCornerPointGridAPI",
        "OmniSciArrayAPI:coord",
        "OmniSciArrayAPI:zcorn",
        "OmniSciArrayAPI:actnum",
        "OmniSciFieldAPI:poro",
        "OmniSciArrayAPI:poro",
        "OmniSciReservoirCellPropertyAPI:poro"
    ]
)
{
    uniform int3  omni:reservoir:grid:logicalCellDims = (60, 220, 85)
    uniform token omni:reservoir:grid:sourceFormat = "grdecl"
    uniform token omni:reservoir:grid:indexOrder = "eclipse"
    uniform token omni:reservoir:grid:depthDirection = "zDown"

    token omni:sci:array:coord:device = "cpu"
    token omni:sci:array:zcorn:device = "cpu"
    token omni:sci:array:actnum:device = "cpu"

    uniform string omni:sci:field:poro:name = "PORO"
    uniform token  omni:sci:field:poro:association = "element"
    token          omni:sci:array:poro:device = "cpu"

    uniform token  omni:reservoir:property:poro:indexSpace = "logicalCells"
    uniform string omni:reservoir:property:poro:sourceKeyword = "PORO"
}
```

#### Property Index Space

`OmniSciReservoirCellPropertyAPI:<name>` records how the value array is packed:

| Value | Meaning |
|-------|---------|
| `logicalCells` | One value per logical cell. Expected length is `NX * NY * NZ`. |
| `activeCells` | One value per active cell, packed in source active-cell order. |
| `unknown` | The reader could not determine the packing convention. |

When a property uses `activeCells`, consumers should use the grid-level
`logicalCellToActiveCell` lookup to place values in the logical grid. Grid
readers author this lookup when `ACTNUM` is present so packed properties can be
wrapped without rebuilding that map. The INIT/UNRST readers preserve native
active-cell packing and expect to compose with the matching grid layer for this
lookup.

### GRDECL Mapping Notes

GRDECL is a text deck format. The implemented reader consumes the grid section
and included property files enough to expose native corner-point arrays and
direct full-cell property arrays.

| GRDECL keyword | Mapping |
|----------------|---------|
| `SPECGRID` / `DIMENS` | `logicalCellDims` |
| `COORD` | `OmniSciArrayAPI:coord` |
| `ZCORN` | `OmniSciArrayAPI:zcorn` |
| `ACTNUM` | `OmniSciArrayAPI:actnum`, optional |
| `ACTNUM`-derived lookup | `OmniSciArrayAPI:logicalCellToActiveCell`, optional when `ACTNUM` is present |
| `MAPAXES` | `omni:reservoir:grid:mapAxes`, optional |
| unit keywords | `omni:reservoir:grid:lengthUnit`, optional |
| property keywords | `OmniSciFieldAPI` + `OmniSciArrayAPI` + `OmniSciReservoirCellPropertyAPI` |

Deck edit operations such as `COPY`, `EQUALS`, `ADD`, and `MULTIPLY` are
recognized only so the parser can skip their records. They are not evaluated,
so arrays whose final values depend on these operations are absent unless they
also appear as direct full-grid keyword arrays.

### EGRID Mapping Notes

EGRID is a binary Eclipse-family grid file. The implemented reader supports
Eclipse unformatted binary item records with 8-character keyword headers,
integer item counts, and `INTE`, `REAL`, `DOUB`, `CHAR`, or `LOGI` payload
types. Decoded grid records are normalized into the same `OmniSciReservoir`
representation used by GRDECL.

| EGRID content | Mapping |
|---------------|---------|
| grid header / dimensions | `logicalCellDims` |
| `COORD` record | `OmniSciArrayAPI:coord` |
| `ZCORN` record | `OmniSciArrayAPI:zcorn` |
| `ACTNUM` record | `OmniSciArrayAPI:actnum`, optional |
| `ACTNUM`-derived lookup | `OmniSciArrayAPI:logicalCellToActiveCell`, optional when `ACTNUM` is present |
| grid name | `omni:reservoir:grid:name`, optional |
| unit / map metadata | `lengthUnit` / `mapAxes`, optional |

The reader exposes `COORD` and `ZCORN` as `double[]` lazy arrays, promoting
`REAL` source records to double for consistency with the GRDECL reader. `ACTNUM`
and `logicalCellToActiveCell` are exposed as `int[]` when ACTNUM is present.
Records after `ENDGRID`, including
`NNCHEAD`, `NNC1`, `NNC2`, LGR records, and multi-grid hierarchy, are not
interpreted in this version. They can be added later without changing the core
global-grid representation.

### INIT Mapping Notes

INIT is a binary Eclipse-family static-property file. The implemented reader is
a pure overlay: it creates an `OmniSciDataset` prim at the same default prim
path as the matching grid case and authors stored cell-sized property records
on that prim.

The reader uses `INTEHEAD` to determine logical dimensions and the active-cell
count. It does not require an EGRID file to parse the INIT layer, but active-cell
packed arrays must compose with a grid layer that provides
`logicalCellToActiveCell` if a consumer wants to recover logical cell ids.

Default `reservoirKeywordMode = "whitelist"` exposes stored numeric property
keywords commonly used as static reservoir cell data:

| Value type | Keywords |
|------------|----------|
| `double[]` | `PORV`, `DEPTH`, `DX`, `DY`, `DZ`, `PORO`, `PERMX`, `PERMY`, `PERMZ`, `NTG`, `TRANX`, `TRANY`, `TRANZ`, `MULTX`, `MULTX-`, `MULTY`, `MULTY-`, `MULTZ`, `SWATINIT`, `SWL`, `SWCR`, `SWU`, `SGL`, `SGCR`, `SGU`, `SOWCR`, `SOGCR`, `KRWR`, `KRGR` |
| `int[]` | `ENDNUM`, `EQLNUM`, `FIPNUM`, `FLUXNUM`, `SATNUM`, `IMBNUM`, `PVTNUM`, `ROCKNUM` |

With `reservoirKeywordMode = "allCellSized"`, the reader exposes numeric
records whose stored item count matches either logical cell count or active
cell count, excluding known header/table records. This is an escape hatch for
project-specific stored keywords; it is not a deck evaluator.

### UNRST Mapping Notes

UNRST is a binary Eclipse-family unified restart file. The implemented reader
groups records by `SEQNUM` and exposes selected stored solution keywords as
time-sampled lazy arrays.

Default `reservoirKeywordMode = "whitelist"` exposes these stored restart
keywords when their record size matches the logical or active cell count:

| Value type | Keywords |
|------------|----------|
| `double[]` | `PRESSURE`, `SWAT`, `SGAS`, `RS`, `RV`, `PBUB`, `PDEW` |

The reader does not synthesize `SOIL`; if an application wants oil saturation,
it should compute it from `SWAT` and `SGAS` outside the file-format layer.

USD time codes are authored in canonical **simulation seconds** and the
emitted layer self-describes the unit by setting
`timeCodesPerSecond = 1.0`. The cross-format `OmniSciFileFormatArgsTimeAPI`
surface (`timeSource` / `timeScale` / `timeOffset`) selects which native
restart concept becomes the time-code axis and how to scale it into
seconds. The plugin's defaults are `(timeSource=TimeValue, timeScale=86400)`,
which read `DOUBHEAD[0]` (simulation days) and multiply by 86400 to land in
seconds.

The legacy per-format `unrstTimeAxis` attribute was removed; existing scenes
should be updated to the generic `TimeAPI` shape using the following mapping:

| Legacy `unrstTimeAxis` | Generic `TimeAPI` | Notes |
|------------------------|-------------------|-------|
| `simulationDays` | `timeSource=TimeValue, timeScale=86400` | Default. `DOUBHEAD[0]` is in simulation days; multiplying by 86400 yields seconds. |
| `reportStep` | `timeSource=IterationValue` | `SEQNUM` is exposed as the iteration counter. Pick `timeScale` to be the average report duration in seconds if real-time playback is desired; otherwise the resulting time codes are an iteration-derived axis. |
| `sampleIndex` | `timeSource=TimeStep` | Zero-based file order. Pick `timeScale` to be the inter-sample `dt` in seconds. |

Host pipelines that compose a UNRST layer through a payload / sublayer /
reference arc into a stage whose `timeCodesPerSecond` is not 1 are
responsible for authoring an
`Sdf.LayerOffset(scale=stage.GetTimeCodesPerSecond(), 0)` on the arc -- the
plugin does not ship a helper API for this. One-line example:

```python
prim.GetPayloads().AddPayload(
    Sdf.Payload(
        assetPath="case.UNRST",
        layerOffset=Sdf.LayerOffset(stage.GetTimeCodesPerSecond(), 0)))
```

With `reservoirKeywordMode = "allCellSized"`, UNRST follows the same escape
hatch as INIT: numeric records matching logical or active cell count may be
exposed, while known header, well/group, and control records are skipped.

## File-Format Arguments

| Reader | Arguments | Mapping effect |
| --- | --- | --- |
| All Eclipse-family readers | `mountPath`, `cacheMode` | Select sublayer placement and lazy value retention. |
| INIT and UNRST | `reservoirKeywordMode` | Select the conservative keyword whitelist or all numeric cell-sized records. |
| UNRST | `timeSource`, `timeScale`, `timeOffset` | Select and convert the restart time axis to simulation seconds. |

`mountPath` is a flat sublayer argument. The other controls are also exposed
through the matching `OmniSciFileFormatArgs*API` schemas.

### Deliberately Out Of Scope

The following concepts are intentionally not represented by this schema and
reader version:

| Concept | Status |
|---------|--------|
| Derived `UsdGeomMesh` | Not authored. Consumers derive externally if needed. |
| `OmniSciCaeMeshAPI` face-vertex arrays | Not applied for GRDECL/EGRID imports. |
| Explicit corner point coordinates | Not authored; derive from `COORD` and `ZCORN` externally. |
| Cell faces, face normals, face areas | Not authored. |
| Cell centroids and volumes | Not authored. |
| Pinch processing and cell removal policy | Not represented in schema. |
| Eclipse edit operations (`COPY`, `EQUALS`, `ADD`, `MULTIPLY`) | Parsed only as skipped blocks; not evaluated. |
| Faults and transmissibility multipliers | Deferred. |
| EGRID NNC records / non-neighbor connections | Ignored after the global-grid geometry section; not interpreted or authored. |
| LGRs / host-grid relationships | Deferred from v1. |
| Automatic sibling discovery / case assembly | Not performed. Compose grid and result files on the same prim explicitly. |
| Derived restart fields such as `SOIL` | Not authored. Consumers may compute them outside the reader. |
| Restart well, group, RFT, summary, and schedule data | Deferred; UNRST v1 only exposes stored cell-sized numeric records. |
| Active-to-logical `globalCellIndex` synthesis for INIT/UNRST | Not authored; compose INIT/UNRST with a grid layer that provides `logicalCellToActiveCell`. |
