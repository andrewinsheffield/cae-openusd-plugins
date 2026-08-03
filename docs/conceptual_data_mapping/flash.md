<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# FLASH AMR and OpenUSD Conceptual Data Mapping

Resolver-backed descriptors are supported when `file` or `files` explicitly
names every HDF5 snapshot. A wildcard `pattern` requires directory enumeration
and is supported only when the descriptor and snapshots are on a local
filesystem.

**Document Version:** 1.0.0
**Last Update:** 2026-07-15

## Introduction

### Overview

This document describes how the implemented `OmniSciFlashFileFormat` plugin
maps PARAMESH-style FLASH HDF5 plotfiles to OpenUSD prims and properties. It is
the conceptual companion to the `OmniSciFlash` schema library and the consumer
reference for the resulting USD representation.

The mapping is **one-way (FLASH → OpenUSD)**. The plugin is read-only;
round-trip write support is not in scope. The document records the supported
subset and current capability gaps.

FLASH stores block-structured adaptive mesh refinement (AMR) data in HDF5
plotfiles. The plugin opens one snapshot or an ordered series through a
repository-defined `.flash` JSON descriptor. OpenUSD represents the source as
one packed `OmniSciDataset` with the following schema composition:

- **OmniSci** (`omni:sci:` namespace) — format-agnostic schemas for scientific
  datasets, fields, and arrays.
- **OmniSciFlash** (`omni:flash:` namespace) — FLASH-specific AMR shape and
  dimensionality metadata.

Large numeric arrays are not loaded eagerly. One `CaeFileFormatData` backend
stores the dataset structure and resolves sampled
`omni:sci:array:<name>:value` attributes on demand.

### References

This document has been prepared in reference to the software and file-layout
conventions listed below.

#### FLASH Reference

| Version | Reference Documents |
|---------|---------------------|
| FLASH 4 HDF5 output format | [FLASH User's Guide: Output Formats](https://flash.rochester.edu/site/flashcode/user_support/flash_ug_devel/node76.html), including the PARAMESH HDF5 records summarized in Table 9.7 |

#### OpenUSD Reference

| Version | Reference Documents |
|---------|---------------------|
| 24.08 | [OpenUSD C++ and Schema Documentation](https://openusd.org/release/api/index.html), [OpenUSD GitHub Repository](https://github.com/PixarAnimationStudios/OpenUSD), [USD Terms and Concepts](https://openusd.org/release/glossary.html) |

### General Assumptions and Constraints

**One-way mapping (FLASH → OpenUSD).** The plugin provides read-only access.
Writing OpenUSD back to FLASH is not supported.

**`.flash` descriptor as the entry point.** Native FLASH plotfiles do not use a
dedicated extension that can be registered reliably with OpenUSD. The plugin
therefore registers `flash` and requires a JSON descriptor that selects exactly
one of `file`, `files`, or `pattern`.

**PARAMESH HDF5 plotfiles only.** The current reader targets the FLASH HDF5
record layout for PARAMESH block-structured AMR. Checkpoint files, AMReX and
Chombo layouts, particle datasets, PnetCDF output, and corner- or
node-interpolated plotfiles are outside the initial supported subset.

**Packed source-block representation.** All source blocks are represented on
one `OmniSciDataset`; the reader does not create one prim per block. Every
structural and physics array retains the complete native HDF5 block order,
including both leaf and internal blocks.

**Source-faithful values.** Numeric types, values, field lookup names, scalar
types, GID references, and optional simulation metadata are preserved. The
reader performs no unit inference, vector grouping, precision conversion,
coordinate-system conversion, refinement filtering, or derived-field
calculation.

**Flattened multidimensional arrays.** OpenUSD array values are flat scalar
arrays. `OmniSciFlashAmrAPI` attributes preserve the source dimensions that
trail the global block dimension.

**Lazy numeric loading.** Structural arrays and physics fields are loaded on
demand. Descriptor parsing, HDF5 dataset discovery, named scalar tables, and
the `sim info` record are read eagerly while the layer is opened.

**Discrete topology samples.** A multi-file descriptor may contain a different
number of AMR blocks at each sample. Values are discrete states; interpolation
between samples with different topology is undefined.

**Time handling is file-format-arg driven.** The default source is the FLASH
`time` scalar. `TimeStep` and `IterationValue` are also supported, followed by
`timeScale` and `timeOffset`. The emitted layer sets `timeCodesPerSecond = 1.0`.

**Root layout.** The default stage layout is `/<descriptorStem>`. `mountPath`
uses the common file-format placement behavior to replace this path.

**Name sanitisation.** Field, scalar, and simulation-information names are
passed through `TfMakeValidIdentifier()` before use as API instance names or
property-name components. Exact source names remain available through
`OmniSciFieldAPI:name` or `flashSourceName` custom data. Sanitised-name
collisions are errors.

### Definitions, Acronyms, Abbreviations

| Term or Abbreviation | Description |
|----------------------|-------------|
| FLASH | A multiphysics simulation code with HDF5 output support and multiple grid implementations. |
| AMR | Adaptive mesh refinement. |
| PARAMESH | The block-structured AMR implementation whose FLASH HDF5 layout is supported by this reader. |
| Plotfile | A FLASH output snapshot containing mesh records and a selected set of physics variables. |
| `.flash` | Repository-defined JSON descriptor used as the OpenUSD file-format entry point. |
| `G` | Global number of source AMR blocks in one snapshot. |
| GID | FLASH global identification table containing block-neighbor, parent, and child references. |
| Leaf block | A block whose native `node type` value is `1`. |
| Internal block | A non-leaf AMR-tree block retained by the source-faithful mapping. |
| Unknown | FLASH terminology for a stored physics variable named by the `unknown names` record. |
| OmniSci | Format-agnostic USD schema plugin (`omni:sci:` namespace) providing scientific dataset, field, and array schemas. |
| OmniSciFlash | FLASH-specific USD schema plugin (`omni:flash:` namespace) providing packed AMR metadata. |
| Lazy attribute | An attribute whose value is resolved on demand from a lazy `SdfAbstractData` backend. |

---

## Concepts

The table below lists the FLASH concepts consumed by the current reader and
their OpenUSD equivalents.

| FLASH | OpenUSD | Description |
|-------|---------|-------------|
| [`.flash` descriptor](#flash-descriptor) | File-format entry layer | Selects one plotfile or an ordered plotfile series. |
| [PARAMESH plotfile](#packed-amr-dataset) | `OmniSciDataset` + `OmniSciFlashAmrAPI` | One packed dataset containing all source blocks. |
| [AMR structure records](#amr-structure-arrays) | `OmniSciArrayAPI:<name>` | Lazy flattened arrays for GID, block classification, bounds, and optional block metadata. |
| [Physics variable](#physics-fields) | `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` | One field/array pair for every entry in `unknown names`. |
| [Named scalar tables](#scalars-and-simulation-information) | Custom attributes under `omni:flash:scalar:` | Typed scalar values retained from the plotfile. |
| [`sim info`](#scalars-and-simulation-information) | Custom attributes under `omni:flash:simInfo:` | Typed simulation and build provenance members. |
| [Plotfile series](#time-varying-data) | USD time samples | Numeric arrays and changing scalar/provenance values sampled at resolved times. |

### FLASH Descriptor

A FLASH dataset is opened through a `.flash` JSON descriptor. The descriptor
must declare `format` and exactly one source selector:

```json
{
  "format": "flash-paramesh-hdf5",
  "version": 1,
  "pattern": "hdf5_plt_cnt_*"
}
```

Paths in `file` and `files` are resolved through the USD Asset Resolver using
the descriptor's original identifier as the anchor. This enables transparent
access to data files on remote storage such as Nucleus when the descriptor
itself is also on that storage. `pattern` uses local filesystem directory
iteration and therefore requires the data files to be on a locally accessible
path; use `file` or `files` for remote storage. `files` preserves explicit
list order; `pattern` uses natural numeric filename order. When the selected
time source is `TimeValue` or `IterationValue`, the resolved snapshots are
subsequently ordered by their native time ordinate.

#### Properties

| Descriptor member | Type | Description |
|-------------------|------|-------------|
| `format` | string | Required; must be `"flash-paramesh-hdf5"`. |
| `version` | integer | Optional; when present, only version `1` is accepted. |
| `file` | string | Selects one plotfile. Mutually exclusive with `files` and `pattern`. |
| `files` | string array | Selects a non-empty explicit plotfile series. Mutually exclusive with `file` and `pattern`. |
| `pattern` | string | Selects plotfiles by a filename wildcard. Mutually exclusive with `file` and `files`. Requires local filesystem access; not supported for remote storage (e.g. Nucleus). |

Duplicate paths, missing files, and patterns that match no files fail the read.

---

### Packed AMR Dataset

The selected plotfile or series becomes one `OmniSciDataset` with
`OmniSciFlashAmrAPI` applied. There is no block-per-prim hierarchy.

**Stage path:** `/<descriptorStem>`

#### Properties

| FLASH | OpenUSD | Description |
|-------|---------|-------------|
| PARAMESH plotfile | `OmniSciDataset` | Typed dataset prim and default prim. |
| packed AMR semantics | `OmniSciFlashAmrAPI` | Marks the dataset as a source-ordered FLASH AMR representation. |
| GID row width | `omni:flash:amr:spatialDimension` (int) | Widths 5, 9, and 15 map to one, two, and three spatial dimensions. |
| structural trailing dimensions | `omni:flash:amr:*Shape` (int[]) | Preserves dimensions omitted by scalar-array flattening. |
| shared field trailing dimensions | `omni:flash:amr:fieldShape` (int[]) | Conventionally `[nzb, nyb, nxb]`. |

#### Composition

```text
/<descriptorStem>    OmniSciDataset  (default prim)
    apiSchemas = [
        "OmniSciFlashAmrAPI",
        "OmniSciArrayAPI:gid",
        "OmniSciArrayAPI:nodeType",
        "OmniSciArrayAPI:refinementLevel",
        "OmniSciArrayAPI:boundingBox",
        ...
    ]
```

---

### AMR Structure Arrays

Every structural array has `G` as its leading source dimension and retains
native HDF5 row order. A FLASH global block id `i` corresponds to source row
`i - 1`. Values within `gid` retain native one-based references and negative
sentinel values.

#### Properties

| FLASH dataset | OpenUSD | Shape metadata | Required |
|---------------|---------|----------------|----------|
| `gid[G, W]` | `OmniSciArrayAPI:gid` + `omni:sci:array:gid:value` | `omni:flash:amr:gidShape = [W]` | Yes |
| `node type[G]` | `OmniSciArrayAPI:nodeType` + `omni:sci:array:nodeType:value` | None | Yes |
| `refine level[G]` | `OmniSciArrayAPI:refinementLevel` + `omni:sci:array:refinementLevel:value` | None | Yes |
| `bounding box[G, 3, 2]` | `OmniSciArrayAPI:boundingBox` + `omni:sci:array:boundingBox:value` | `omni:flash:amr:boundingBoxShape = [3, 2]` | Yes |
| `coordinates[G, 3]` | `OmniSciArrayAPI:coordinates` + `omni:sci:array:coordinates:value` | `omni:flash:amr:coordinatesShape = [3]` | No |
| `block size[G, 3]` | `OmniSciArrayAPI:blockSize` + `omni:sci:array:blockSize:value` | `omni:flash:amr:blockSizeShape = [3]` | No |
| `processor number[G]` | `OmniSciArrayAPI:processorNumber` + `omni:sci:array:processorNumber:value` | None | No |

The exact trailing sizes of optional coordinate-related datasets are preserved
from the source rather than forced to a logical-dimensionality shape.

#### Metadata

`OmniSciArrayAPI::CreateDeviceAttr()` is `"cpu"` for all structural arrays.
The array value type matches the supported native HDF5 numeric type.

#### Composition

All structural values are registered in the combined backend. A
single-snapshot descriptor exposes one sample; a series exposes one value per
time sample.

---

### Physics Fields

Each entry in the required `unknown names` record identifies one root HDF5
dataset. The reader represents every entry as an `OmniSciFieldAPI:<name>` and
`OmniSciArrayAPI:<name>` pair on the packed dataset.

#### Properties

| FLASH | OpenUSD | Description |
|-------|---------|-------------|
| exact unknown name | `omni:sci:field:<name>:name` (string) | Preserves the exact HDF5 dataset lookup name. |
| cell-centered block data | `omni:sci:field:<name>:association = "element"` | Marks values as element-associated. |
| variable dataset | `omni:sci:array:<name>:value` | Lazy flat scalar array preserving the native numeric type and order. |
| source shape `[G, nzb, nyb, nxb]` | `omni:flash:amr:fieldShape = [nzb, nyb, nxb]` | Shared trailing shape for all physics fields. |

The API instance `<name>` is a valid USD identifier. The `name` attribute is
the authoritative source lookup name and may differ from the instance after
sanitisation.

Values are flattened in native `[G, nzb, nyb, nxb]` order, with X varying
fastest. Values for internal blocks are retained even though `node type == 1`
identifies leaf blocks used conventionally for plotting.

#### Metadata

`OmniSciArrayAPI::CreateDeviceAttr()` is `"cpu"` for all physics fields.

#### Composition

Fields are authored directly on the packed dataset. The reader does not group
component-like names into vectors or create block child prims.

---

### Scalars and Simulation Information

Named entries from `integer scalars`, `real scalars`, `logical scalars`, and
`string scalars` become typed custom attributes on the packed dataset. Members
of the optional `sim info` record use the same mapping convention.

#### Properties

| FLASH | OpenUSD | Description |
|-------|---------|-------------|
| named scalar | `omni:flash:scalar:<validName>` | Custom attribute with the matching scalar USD type. |
| `sim info` member | `omni:flash:simInfo:<validName>` | Custom attribute preserving the member value and type. |
| exact source name | `customData["flashSourceName"]` | Original name, including whitespace or characters changed by sanitisation. |

Supported scalar values map to the corresponding USD `int`, `int64`, `uint`,
`uint64`, `float`, `double`, `bool`, or `string` attribute type.

For a series, values that remain constant are authored as defaults. Values that
change are authored as time samples.

---

### Time-Varying Data

A descriptor containing one plotfile authors numeric arrays as default values.
A descriptor containing multiple plotfiles authors all numeric arrays as USD
time samples. Array lengths may vary as the AMR tree changes.

#### Properties

| FLASH | OpenUSD | Description |
|-------|---------|-------------|
| descriptor/list position | USD time ordinate with `timeSource=TimeStep` | Zero-based position before scale and offset. |
| `time` scalar | USD time ordinate with `timeSource=TimeValue` | Stored physical time selected explicitly. |
| `nstep` scalar | USD time ordinate with `timeSource=IterationValue` | Stored simulation iteration. |
| structural and field arrays | Time samples on `omni:sci:array:<name>:value` | One complete source state per resolved time. |
| changing scalar or `sim info` value | Time samples on its custom attribute | Constant values remain defaults. |

#### Series Compatibility

All snapshots in one series must retain:

- field names and field ordering
- spatial dimension and file-format version
- numeric types and trailing array shapes
- the set of optional structural arrays
- scalar and `sim info` names and types

The leading block dimension may change. Duplicate resolved time codes are
errors. `TimeValue` requires a numeric `time` scalar in every snapshot;
`IterationValue` requires a numeric `nstep` scalar.

#### Composition

For `TimeValue` and `IterationValue`, native ordinates come from the matching
scalar. `TimeStep` assigns raw ordinates in descriptor/list or natural-pattern
order. After scale and offset are applied, all samples are registered in
ascending resolved USD time-code order, including when `timeScale` is negative.

---

## Appendices

### Appendix A: File Format Arguments

`OmniSciFlashFileFormat` accepts the following public arguments when opening a
layer.

| Argument | Type | Default | Description |
|----------|------|---------|-------------|
| `timeSource` | string | `"TimeStep"` | Native time source: `"TimeStep"` uses zero-based descriptor order, `"TimeValue"` uses `time`, and `"IterationValue"` uses `nstep`. Reachable through the shared `OmniSciFileFormatArgsTimeAPI:source`; `OmniSciFileFormatArgsFlashAPI` applies that API for convenience. |
| `timeScale` | float | `1.0` | Multiplier applied to the selected native source. Also reachable via `OmniSciFileFormatArgsTimeAPI:scale`. |
| `timeOffset` | float | `0.0` | Additive offset applied after `timeScale`. Also reachable via `OmniSciFileFormatArgsTimeAPI:offset`. |
| `cacheMode` | string | `"all"` | Lazy-array retention policy: `"all"`, `"static"`, or `"none"`. Also reachable via `OmniSciFileFormatArgsAPI:cacheMode`. |
| `mountPath` | string | descriptor-stem root | Absolute prim path used for sublayer placement. Flat layer argument only. |

### Appendix B: Stage Layout Example

A descriptor named `series.flash` containing `dens` and `velx` fields maps to:

```text
/series                              OmniSciDataset  (default prim)
                                       apiSchemas = [
                                         "OmniSciFlashAmrAPI",
                                         "OmniSciArrayAPI:gid",
                                         "OmniSciArrayAPI:nodeType",
                                         "OmniSciArrayAPI:refinementLevel",
                                         "OmniSciArrayAPI:boundingBox",
                                         "OmniSciArrayAPI:coordinates",
                                         "OmniSciFieldAPI:dens",
                                         "OmniSciArrayAPI:dens",
                                         "OmniSciFieldAPI:velx",
                                         "OmniSciArrayAPI:velx"
                                       ]
                                       omni:flash:amr:spatialDimension = 2
                                       omni:flash:amr:gidShape = [9]
                                       omni:flash:amr:boundingBoxShape = [3, 2]
                                       omni:flash:amr:coordinatesShape = [3]
                                       omni:flash:amr:fieldShape = [1, 8, 8]
                                       omni:sci:field:dens:name = "dens"
                                       omni:sci:field:dens:association = "element"
                                       omni:sci:array:dens:device = "cpu"
                                       omni:flash:scalar:nxb = 8
                                       omni:flash:simInfo:flash_version = "FLASH-X"
                                       -- lazy, time sampled --
                                       omni:sci:array:gid:value = int[]
                                       omni:sci:array:nodeType:value = int[]
                                       omni:sci:array:boundingBox:value = double[]
                                       omni:sci:array:dens:value = double[]
                                       omni:sci:array:velx:value = double[]
```

All content is carried on one prim; the source AMR blocks are array records,
not child prims.

### Appendix C: Capability Gaps

The following FLASH concepts are outside the current mapping.

| FLASH concept | Gap Description | Notes |
|---------------|-----------------|-------|
| Checkpoint files | Not supported. | The initial reader contract targets plotfiles. |
| AMReX, Chombo, and uniform-grid layouts | Not supported. | Only PARAMESH-style HDF5 records are recognized. |
| Particle datasets | Not supported. | Unrecognized root datasets produce warnings. |
| PnetCDF output | Not supported. | The implementation uses the HDF5 C API. |
| Corner- or node-interpolated plotfiles | Not supported. | The mapping assumes cell-centered block fields. |
| Per-block prim hierarchy | Not represented. | Blocks remain packed in source-ordered arrays. |
| Leaf-only or maximum-refinement filtering | Not implemented. | All source blocks are retained. |
| Vector grouping and derived fields | Not implemented. | Every unknown remains an independent scalar array. |
| 8-bit and 16-bit numeric arrays | Not supported. | The source-faithful mapping accepts 32-bit and 64-bit integer and floating-point arrays without silent widening. |
| Unit and coordinate-system inference | Not implemented. | All stored axes and values remain source faithful. |
| Writing OpenUSD back to FLASH | Not supported. | The plugin is read-only. |
| `pattern` on remote storage | Not supported. | Directory iteration requires a local filesystem. Use `file` or `files` for descriptors on Nucleus or other remote storage. |

The minimal required HDF5 records are `unknown names`, `gid`, `node type`,
`refine level`, `bounding box`, and every field dataset named by
`unknown names`. Missing or shape-inconsistent required data fails the read.
