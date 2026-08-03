<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Schema Libraries

Schema plugins provide the vocabulary used by the native file-format readers.
They describe datasets, fields, arrays, and domain semantics; they do not parse
source files or own array storage.

All schema sources live under `source/schemas/` and are built independently of
the optional reader switches. Generated Python bindings are installed only
when `CAE_BUILD_SCHEMA_PYTHON_BINDINGS` is enabled and a compatible Python
development runtime is available.

## Generated Names

Each schema library has related names at different API layers. For example:

| API layer | Example |
| --- | --- |
| Schema library and plugin | `omniSci` |
| Generated Python module | `pxr.OmniSci` |
| USD schema identifier | `OmniSciDataset` |
| Python class | `OmniSci.Dataset` |
| C++ class | `OmniSciDataset` |

Register the installed plugin bundle before importing generated modules. See
[Using the plugins](../usage.md#register-the-plugins).

## Library Index

| Library and source | Python module | Principal schemas | Semantics |
| --- | --- | --- | --- |
| [`omniSci`](../../source/schemas/omni_sci/schema.usda) | `pxr.OmniSci` | `Dataset`, `FieldAPI`, `ArrayAPI` | Format-independent datasets, named fields, and typed arrays; see [Core OmniSci schemas](omni_sci.md). |
| [`omniSciCae`](../../source/schemas/omni_sci_cae/schema.usda) | `pxr.OmniSciCae` | `MeshAPI`, `PointCloudAPI` | Shared CAE mesh and point-cloud models used by [EDEM](../conceptual_data_mapping/edem.md), [NumPy](../conceptual_data_mapping/numpy.md), and [Trimesh](../conceptual_data_mapping/trimesh.md). |
| [`omniSciCgns`](../../source/schemas/omni_sci_cgns/schema.usda) | `pxr.OmniSciCgns` | `ZoneAPI`, `GridCoordinatesAPI`, `FlowSolutionAPI`, `UnstructuredElementsAPI` | [CGNS/SIDS concepts](../conceptual_data_mapping/cgns.md). |
| [`omniSciEdem`](../../source/schemas/omni_sci_edem/schema.usda) | `pxr.OmniSciEdem` | `ParticleCloudAPI`, `ParticleTypeAPI`, `GeometryGroupAPI` | [EDEM particle and geometry groups](../conceptual_data_mapping/edem.md). |
| [`omniSciEnSight`](../../source/schemas/omni_sci_ensight/schema.usda) | `pxr.OmniSciEnSight` | `Piece`, `UnstructuredPartAPI`, `UnstructuredPieceAPI` | [EnSight Gold parts and topology pieces](../conceptual_data_mapping/ensight.md). |
| [`omniSciFlash`](../../source/schemas/omni_sci_flash/schema.usda) | `pxr.OmniSciFlash` | `AmrAPI` | [Packed, source-order FLASH AMR metadata](../conceptual_data_mapping/flash.md). |
| [`omniSciOpenFoam`](../../source/schemas/omni_sci_openfoam/schema.usda) | `pxr.OmniSciOpenFoam` | `PolyMeshAPI`, `BoundaryPatchAPI` | [OpenFOAM face-based topology and patches](../conceptual_data_mapping/openfoam.md). |
| [`omniSciReservoir`](../../source/schemas/omni_sci_reservoir/schema.usda) | `pxr.OmniSciReservoir` | `CornerPointGridAPI`, `CellPropertyAPI` | [Eclipse-family reservoir geometry and property packing](../conceptual_data_mapping/grdecl_egrid.md). |
| [`omniSciVtk`](../../source/schemas/omni_sci_vtk/schema.usda) | `pxr.OmniSciVtk` | `UnstructuredGridAPI`, `StructuredGridAPI`, `ImageDataAPI`, `RectilinearGridAPI`, `PolyDataAPI` | [VTK dataset kinds and topology](../conceptual_data_mapping/vtk.md). |
| [`omniSciFileFormatArgs`](../../source/schemas/omni_sci_file_format_args/schema.usda) | `pxr.OmniSciFileFormatArgs` | Shared and format-specific argument APIs | Typed configuration for [dynamic file-format payloads](../file_formats/arguments.md). |

## Composition Model

`OmniSciDataset` is the common concrete dataset type. Named field and array
APIs, plus any applicable domain APIs, compose on that prim or its descendants.
Most domain schemas are single-apply APIs; `OmniSciFieldAPI`,
`OmniSciArrayAPI`, and `OmniSciReservoirCellPropertyAPI` are multiple-apply
APIs whose instance names distinguish logical fields, arrays, or properties.
`OmniSciEnSightPiece` is a concrete typed prim for an EnSight topology block.

The relevant [Conceptual Data Mapping](../conceptual_data_mapping/README.md)
defines which schemas a reader applies, where it applies them, and how their
properties relate to the source format.

## Sources of Truth

- Each library's `schema.usda` defines its public schema classes, properties,
  types, defaults, allowed tokens, and generated API names.
- [Conceptual Data Mappings](../conceptual_data_mapping/README.md) define how
  readers compose those schemas into observable USD stages.
- [File-format arguments](../file_formats/arguments.md) documents the typed
  payload APIs and their flat layer-argument equivalents.
- Schema and reader tests verify generated bindings, round trips, and applied
  schema behavior.

These guides explain intended composition and usage; they do not replace the
generated C++ or Python API documentation.
