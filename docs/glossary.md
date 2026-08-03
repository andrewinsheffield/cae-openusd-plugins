<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Glossary

These definitions describe how terms are used in this project. For general USD
concepts, they emphasize the behavior relevant to native scientific-data
readers.

**API schema.** A non-concrete schema applied to a prim to add a defined set of
properties and semantics without changing the prim's concrete type. This
project composes core and domain-specific API schemas on dataset prims.

**Array.** A named collection of scalar, vector, or other typed values.
`OmniSciArrayAPI:<instance>` describes the array, while
`omni:sci:array:<instance>:value` holds or lazily supplies its typed value. See
[Core OmniSci schemas](schemas/omni_sci.md#omnisciarrayapiinstance).

**Association.** The topology entity to which a field's tuples belong.
`OmniSciFieldAPI:<instance>` uses `node` for points or nodes, `element` for
cells, elements, faces, or particles as defined by the domain mapping, and
`none` when the field has no topological association.

**Conceptual Data Mapping.** The USD-facing contract for one source format. It
defines the resulting hierarchy, schemas, properties, array types and shapes,
time samples, fidelity guarantees, and known capability gaps. See
[Conceptual Data Mappings](conceptual_data_mapping/README.md).

**Dataset.** One logical scientific or engineering dataset. It may be
represented by an `OmniSciDataset` prim or, when the source format requires
containers, by a hierarchy rooted at the layer's default prim.

**Default prim.** The prim selected by a layer's `defaultPrim` metadata. When a
native asset is used as a payload without an explicit prim path, this is the
prim composed onto the payload-bearing prim.

**Field.** A named physical or computed quantity.
`OmniSciFieldAPI:<instance>` records its source name and association. A field
normally shares its instance name with the `OmniSciArrayAPI` that supplies its
values.

**File-format argument.** A string-valued option attached to an asset
identifier and passed to `SdfFileFormat` when a layer is opened. Some arguments
can also be authored as typed API-schema attributes on a payload-bearing prim.
See [File-format arguments](file_formats/arguments.md).

**File series.** A sequence of native assets representing different simulation
states. USD value clips can map the files' local samples onto a host timeline
without first converting the source files to USD. See
[File series and USD value clips](file_formats/file_series.md).

**Heavy data.** Array values large or expensive enough to load only when
requested. Heavy data is still presented to consumers as ordinary typed USD
attribute values.

**Lazy value.** A USD attribute value whose declaration and metadata are
available when the layer opens but whose backing array is read only when the
value is requested. Native readers expose potentially varying lazy values as
time samples, including a single-state value at time `0`.

**Mount path.** The absolute prim path supplied through the flat `mountPath`
file-format argument. It replaces the reader's filename-derived root when a
stable hierarchy is needed for sublayers or value clips. It is not a typed
payload argument because payload composition already places the source
layer's default prim at the payload-bearing prim.

**Multiple-apply API.** An API schema that can be applied to the same prim
under several instance names. `OmniSciFieldAPI:pressure` and
`OmniSciArrayAPI:pressure`, for example, describe the semantics and storage of
one named field without preventing other fields from using the same prim.

**Native asset.** A supported scientific or engineering source asset, such as
a VTK or CGNS file, opened directly as a USD layer through an
`SdfFileFormat` plugin. The native asset remains the source of truth.

**Resolver-backed asset.** An asset identifier opened through the
application's active `ArResolver`. It may refer to a local file or to remote
storage. The application supplies support for the identifier scheme; the
file-format plugins do not embed storage-specific clients.

**Simulation seconds.** The canonical time-code unit emitted by time-aware
readers. Native-format layers author `timeCodesPerSecond = 1.0`; source values
can be scaled and offset into seconds. See
[Time handling](file_formats/time.md).

**Source fidelity.** Preserving source data types, array shapes, indexing,
topology, units, and names where practical instead of silently converting them
to a visualization-specific representation.

**Structure data.** Prim hierarchy, applied schemas, relationships, attribute
declarations, and lightweight metadata that can be discovered without
materializing heavy arrays.

**Typed payload argument.** A uniform attribute from a format-specific API
schema applied to a payload-bearing prim. OpenUSD derives file-format arguments
from these attributes; editing one recomposes and rereads the dynamic payload
without requiring the payload to be removed or explicitly reloaded.

**Value clip.** USD composition metadata that supplies time-varying attribute
values from one or more external layers. Clip assets provide values but do not
define the topology and field structure required to interpret them.
