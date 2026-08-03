<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# VTK and OpenUSD Conceptual Data Mapping

Resolver-backed identifiers are supported for self-contained legacy and XML
VTK files. Implicit external sidecars are outside this contract.

**Document Version:** 1.0.0
**Last Update:** 2026-04-19

## Introduction

### Overview

This document describes how VTK dataset concepts map into OpenUSD prims and
properties in this repository. It is the conceptual reference for the
implemented VTK readers:

- `OmniSciVtkFileFormat` for legacy `.vtk` and serial XML VTK `.vti`, `.vtr`,
  `.vts`, `.vtp`, and `.vtu`

The mapping is **one-way (VTK → OpenUSD)**. The readers are read-only;
round-trip write support is not in scope. This document also calls out the
remaining capability gaps where familiar VTK concepts are not yet surfaced.

VTK is a family of dataset models rather than a single topology convention, so
the OpenUSD representation is organised around a small set of VTK-specific API
schemas layered on top of the repository's format-agnostic scientific schemas:

- **OmniSci** (`omni:sci:` namespace) — format-agnostic base schemas for
  scientific datasets, fields, and arrays.
- **OmniSciVtk** (`omni:vtk:` namespace) — VTK-specific API schemas for
  unstructured grids, structured grids, image data, rectilinear grids, and
  poly data.

Unlike the old `kit-cae` VTK schema set, this repository does **not** mirror a
separate typed `CaeVtkFieldArray` prim. Instead, VTK geometry, topology, and
field arrays are expressed as `OmniSciArrayAPI` instances on the owning
dataset prim, using VTK-specific library tokens such as `points`,
`connectivityOffsets`, `connectivityArray`, `cellTypes`, and the poly-data
connectivity names.

### References

This document has been prepared in reference to the software and schema
conventions listed below.

#### VTK Reference

| Version | Reference Documents |
|---------|---------------------|
| VTK 9.x conceptual dataset model | [VTK Book](https://vtk.org/documentation/), [VTK Data Model](https://examples.vtk.org/site/VTKBook/05Chapter5/) |

#### OpenUSD Reference

| Version | Reference Documents |
|---------|---------------------|
| 24.08 | [OpenUSD C++ and Schema Documentation](https://openusd.org/release/api/index.html), [OpenUSD GitHub Repository](https://github.com/PixarAnimationStudios/OpenUSD), [USD Terms and Concepts](https://openusd.org/release/glossary.html) |

### General Assumptions and Constraints

**Read-only mapping.** This document describes the OpenUSD mapping used by the
implemented VTK readers and the schemas that support that mapping.

**One-way mapping (VTK → OpenUSD).** The readers provide read-only access.
Writing OpenUSD back to VTK files is not supported.

**Schema composition over inheritance.** VTK-specific dataset
metadata is added through API schemas, not by introducing format-specific
typed dataset prims. The owning prim is an `OmniSciDataset` with one of the VTK
API schemas applied.

**VTK-native array names.** Geometry and topology arrays are carried through
`OmniSciArrayAPI` instances using VTK-specific library tokens declared in the
`omniSciVtk` schema library. This preserves familiar VTK terminology and avoids
reintroducing a parallel field-array schema hierarchy.

**Root layout.** The VTK readers follow the same repository convention as
the other CAE file formats: the layer's default prim is the dataset itself,
authored at `/<filename-stem>`.

**Field ownership follows dataset ownership.** Point data and cell data are
authored on the owning dataset prim as `OmniSciFieldAPI:<name>` +
`OmniSciArrayAPI:<name>` pairs. Association distinguishes point-centred from
cell-centred arrays.

**Dataset-family-specific topology.** The mapping intentionally keeps topology
representation close to VTK's own dataset families:

- unstructured grids use offsets + connectivity + cell types
- structured grids use explicit points plus ijk extents
- image data uses origin + spacing + ijk extents with implicit points
- rectilinear grids use x/y/z coordinate arrays plus ijk extents
- poly data uses separate connectivity arrays for verts, lines, polys, and strips

### Definitions, Acronyms, Abbreviations

| Term or Abbreviation | Description |
|----------------------|-------------|
| VTK | The Visualization Toolkit and its family of scientific dataset models. |
| Unstructured grid | VTK dataset family representing arbitrary cell topologies with explicit connectivity. |
| Structured grid | VTK dataset family using logically structured ijk topology with explicit points. |
| Image data | VTK dataset family using implicit points defined by origin, spacing, and ijk extents. |
| Poly data | VTK dataset family for points, vertices, lines, polygons, and triangle strips. |
| Point data | VTK arrays associated with dataset points. |
| Cell data | VTK arrays associated with dataset cells. |
| OmniSci | Format-agnostic USD schema plugin (`omni:sci:` namespace) providing base types for scientific datasets. |
| OmniSciVtk | VTK-specific USD schema plugin (`omni:vtk:` namespace) providing API schemas for VTK dataset families. |
| API schema | A non-typed USD schema that adds a reusable set of properties to any prim via `apiSchemas`. |

---

## Concepts

The table below lists the VTK dataset concepts currently covered by the schema
pass and their intended OpenUSD equivalents.

| VTK | OpenUSD | Description |
|-----|---------|-------------|
| [Dataset root](#dataset-root) | `OmniSciDataset` at `/<filename-stem>` (default prim) | The dataset prim itself anchors the layer; family-specific API schema applied. |
| [Unstructured grid](#unstructured-grid) | `OmniSciDataset` + `OmniSciVtkUnstructuredGridAPI` | Explicit points, connectivity, offsets, and cell types. |
| [Structured grid](#structured-grid) | `OmniSciDataset` + `OmniSciVtkStructuredGridAPI` | Explicit points plus ijk extents. |
| [Image data](#image-data) | `OmniSciDataset` + `OmniSciVtkImageDataAPI` | Implicit regular grid defined by origin, spacing, and ijk extents. |
| [Rectilinear grid](#rectilinear-grid) | `OmniSciDataset` + `OmniSciVtkRectilinearGridAPI` | Axis-aligned rectilinear coordinates plus ijk extents. |
| [Poly data](#poly-data) | `OmniSciDataset` + `OmniSciVtkPolyDataAPI` | Explicit points plus separate verts/lines/polys/strips topology arrays. |
| [Point data arrays](#fields) | `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` | Field arrays associated with dataset points. |
| [Cell data arrays](#fields) | `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` | Field arrays associated with dataset cells. |
| Legacy `CaeVtkFieldArray` typed prim | | **Not used in this repository.** VTK arrays live directly on the owning dataset prim as `OmniSciArrayAPI` instances. |

### Dataset Root

The VTK readers anchor imported content at `/<filename-stem>`, which is the
layer's default prim. The dataset prim itself is the layer root — there is
no separate wrapper Scope. Per-family API schemas
(`OmniSciVtkUnstructuredGridAPI`, `OmniSciVtkPolyDataAPI`, etc.) are applied
on this prim to carry the dataset family.

**Stage path:** `/<filename-stem>`

#### Composition

```text
/<filename-stem>          OmniSciDataset (default prim)
                            apiSchemas = ["OmniSciVtk<Family>API", ...]
```

A single-dataset file maps directly to one `OmniSciDataset` prim at the
layer root with the family-specific API schema applied. Children of that
prim hold `Cells` / `PointData` / `CellData` blocks as needed.

---

### Unstructured Grid

A VTK unstructured grid is represented by an `OmniSciDataset` prim with
`OmniSciVtkUnstructuredGridAPI` applied. Points and topology arrays are carried
on the same prim through `OmniSciArrayAPI` instances using the VTK-native token
names declared by `omniSciVtk`.

#### Properties

| VTK | OpenUSD | Description |
|-----|---------|-------------|
| point coordinates | `OmniSciArrayAPI:points` | Explicit point positions. |
| cell offsets | `OmniSciArrayAPI:connectivityOffsets` | Offsets array for the flat connectivity buffer. |
| cell connectivity | `OmniSciArrayAPI:connectivityArray` | Flat point-id list for all cells. |
| packed cell connectivity | `OmniSciArrayAPI:connectivityPackedArray` | Legacy packed cell array in source order: per-cell point count followed by point ids. |
| cell types | `OmniSciArrayAPI:cellTypes` | VTK numeric cell-type ids. |
| polyhedron face offsets | `OmniSciArrayAPI:polyhedronFacesOffsets` | Optional polyhedron face-offset array. |
| polyhedron face connectivity | `OmniSciArrayAPI:polyhedronFacesConnectivityArray` | Optional polyhedron face connectivity buffer. |
| polyhedron face-location offsets | `OmniSciArrayAPI:polyhedronFaceLocationsOffsets` | Optional offsets locating faces per cell. |
| polyhedron face-location connectivity | `OmniSciArrayAPI:polyhedronFaceLocationsConnectivityArray` | Optional face-location connectivity buffer. |
| packed polyhedron faces | `OmniSciArrayAPI:polyhedronPackedFaces` | Legacy VTK XML packed polyhedron face stream. |
| packed polyhedron face offsets | `OmniSciArrayAPI:polyhedronPackedFaceOffsets` | Legacy VTK XML per-cell endpoints into the packed face stream; non-polyhedron cells may use `-1`. |

This layout is intended to cover both ordinary unstructured cells and VTK's
more complex polyhedron encoding without introducing a VTK-only array prim
type. The file format preserves the topology representation used by the source:
split source arrays stay split, and packed source arrays stay packed for
application-side decoding.

---

### Structured Grid

A VTK structured grid is represented by an `OmniSciDataset` prim with
`OmniSciVtkStructuredGridAPI` applied.

#### Properties

| VTK | OpenUSD | Description |
|-----|---------|-------------|
| point coordinates | `OmniSciArrayAPI:points` | Explicit points ordered by VTK structured-grid convention. |
| whole extent min | `omni:vtk:minExtent` | Minimum ijk extent. |
| whole extent max | `omni:vtk:maxExtent` | Maximum ijk extent. |

Structured grids use explicit points, but their topology is implied by the ijk
extents rather than by a separate connectivity buffer.

---

### Image Data

A VTK image-data dataset is represented by an `OmniSciDataset` prim with
`OmniSciVtkImageDataAPI` applied.

#### Properties

| VTK | OpenUSD | Description |
|-----|---------|-------------|
| origin | `omni:vtk:origin` | Dataset origin. |
| spacing | `omni:vtk:spacing` | Grid spacing along each axis. |
| whole extent min | `omni:vtk:minExtent` | Minimum ijk extent. |
| whole extent max | `omni:vtk:maxExtent` | Maximum ijk extent. |

Unlike structured grids, image data does not carry an explicit points array;
point locations are implicit from origin, spacing, and extent.

---

### Rectilinear Grid

A VTK rectilinear-grid dataset is represented by an `OmniSciDataset` prim with
`OmniSciVtkRectilinearGridAPI` applied.

#### Properties

| VTK | OpenUSD | Description |
|-----|---------|-------------|
| x coordinates | `OmniSciArrayAPI:xCoordinates` | Explicit coordinate values along X. |
| y coordinates | `OmniSciArrayAPI:yCoordinates` | Explicit coordinate values along Y. |
| z coordinates | `OmniSciArrayAPI:zCoordinates` | Explicit coordinate values along Z. |
| whole extent min | `omni:vtk:minExtent` | Minimum ijk extent. |
| whole extent max | `omni:vtk:maxExtent` | Maximum ijk extent. |

Rectilinear grids use independent coordinate arrays along each axis rather
than a full explicit points array or an implicit image-data spacing model.

---

### Poly Data

A VTK poly-data dataset is represented by an `OmniSciDataset` prim with
`OmniSciVtkPolyDataAPI` applied.

#### Properties

| VTK | OpenUSD | Description |
|-----|---------|-------------|
| point coordinates | `OmniSciArrayAPI:points` | Explicit point positions. |
| verts offsets/connectivity | `OmniSciArrayAPI:vertsConnectivityOffsets`, `OmniSciArrayAPI:vertsConnectivityArray` | Vertex-cell topology. |
| lines offsets/connectivity | `OmniSciArrayAPI:linesConnectivityOffsets`, `OmniSciArrayAPI:linesConnectivityArray` | Polyline topology. |
| polys offsets/connectivity | `OmniSciArrayAPI:polysConnectivityOffsets`, `OmniSciArrayAPI:polysConnectivityArray` | Polygon topology. |
| strips offsets/connectivity | `OmniSciArrayAPI:stripsConnectivityOffsets`, `OmniSciArrayAPI:stripsConnectivityArray` | Triangle-strip topology. |
| packed verts/lines/polys/strips | `OmniSciArrayAPI:vertsPackedConnectivityArray`, `OmniSciArrayAPI:linesPackedConnectivityArray`, `OmniSciArrayAPI:polysPackedConnectivityArray`, `OmniSciArrayAPI:stripsPackedConnectivityArray` | Legacy packed polydata topology in source order: per-cell point count followed by point ids. |

Each topology family is optional; a given poly-data dataset may author any
subset of verts, lines, polys, and strips.

---

### Fields

VTK point data and cell data are represented on the owning dataset prim as
`OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` pairs.

#### Properties

| VTK | OpenUSD | Description |
|-----|---------|-------------|
| point-data array | `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` | Array associated with dataset points. |
| cell-data array | `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` | Array associated with dataset cells. |

The array values and field metadata are intentionally modeled using the shared
OmniSci schemas rather than a VTK-specific field-array prim type.

---

## File-Format Coverage

The current VTK support in this repository covers:

- legacy VTK `.vtk`
- serial XML VTK `.vti`, `.vtr`, `.vts`, `.vtp`, `.vtu`
- ASCII and binary legacy files
- XML inline ASCII, inline binary, and appended binary payloads
- lazy heavy-data reads for XML appended raw payloads
- lazy heavy-data reads for XML appended `encoding="base64"` payloads
- lazy heavy-data reads for XML inline `format="binary"` payloads
- XML compressor support:
  - `vtkLZ4DataCompressor` through external LZ4
  - `vtkZLibDataCompressor`
  - `vtkLZMADataCompressor`

The XML plugin itself is enabled by default. LZ4, zlib, and LZMA are required
external dependencies whenever the VTK plugin is built.

## File-Format Arguments

| Argument | Default | Mapping effect |
| --- | --- | --- |
| `mountPath` | filename-stem root | Places the dataset at an absolute sublayer path. |
| `cacheMode` | `all` | Controls lazy value retention (`all`, `static`, or `none`). |
| `ioThreads` | `1` | Caps worker tasks used for large parallelizable file reads. |

Payload grain sizes are implementation details rather than file-format
arguments.

## Capability Gaps

The main VTK items still left for follow-on implementation are:

- parallel XML VTK wrappers such as `.pvti`, `.pvtr`, `.pvts`, `.pvtp`, `.pvtu`
- multiblock and composite datasets
- richer `FieldData` handling for arrays that are neither point data nor cell data
- time-varying VTK file-series conventions
- any additional less-common dataset object families beyond the currently
  implemented legacy and serial XML coverage
