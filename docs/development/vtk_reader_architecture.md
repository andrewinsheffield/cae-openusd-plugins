<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# VTK Reader Architecture

> **Status:** Implemented for legacy VTK and serial VTK XML
>
> **Audience:** Reader maintainers

The unified VTK plugin exposes legacy `.vtk` and serial XML `.vti`, `.vtr`,
`.vts`, `.vtp`, and `.vtu` assets through one `SdfFileFormat`. This document
records the internal contracts that are important for correctness and
performance.

The user-visible dataset mapping, supported storage envelopes, arguments, and
capability gaps belong in the
[VTK Conceptual Data Mapping](../conceptual_data_mapping/vtk.md). General
guidance shared by other readers belongs in
[File Format Performance Patterns](file_format_performance.md).

## Layered Design

```text
OmniSciVtkFileFormat
  -> DatasetSpecCache
    -> LegacyParser or XmlParser
      -> immutable DatasetSpec and ArraySpec metadata
  -> USD structure authoring
  -> CaeFileFormatData lazy time-sample registration
    -> ArrayEvaluator
      -> ScalarPayloadReader
        -> PayloadSegmentReader
          -> source file ranges
```

Each layer has one responsibility:

- Parsers discover dataset structure and translate storage-specific locations
  into immutable metadata.
- The file-format layer authors the USD hierarchy and registers lazy values.
- Array evaluation maps stored scalars into the final USD-facing shape.
- Payload readers decode one scalar payload into caller-owned storage.
- Segment readers remove ASCII, raw-binary, or base64 storage encoding.

The parser and payload layers do not author USD. The file-format layer does not
interpret VTK byte layouts.

## Immutable Parsed Metadata

`DatasetSpec` records:

- file-level byte order, XML header type, and compressor;
- dataset family and structural metadata;
- array names, associations, roles, scalar types, tuple counts, and component
  counts;
- absolute source segments for every lazy scalar payload;
- enough context to diagnose malformed or unsupported arrays.

It does not retain decoded arrays, open file handles, or heavy inline text.

The process-local `DatasetSpecCache` stores immutable specs using a file
signature composed from resolved path, file size, modification time, and a
small content fingerprint. Concurrent misses for one file use single-flight
parsing. Runtime options such as `mountPath`, `cacheMode`, and `ioThreads` do
not fork the parsed metadata because they do not alter its meaning.

## Payload Coordinates

Every parsed payload is reduced to one or more absolute source-file segments:

```text
PayloadSourceSpec
  storage kind: ASCII | plain binary | XML binary | XML base64 binary
  segments:
    start offset
    source byte count
```

Runtime readers never receive legacy record coordinates or unresolved XML
`DataArray` offsets.

Direct ASCII and binary payloads use one segment. An uncompressed XML binary
block also uses one segment containing its size header and payload. A
compressed XML block uses:

1. the compression header and compressed-size table;
2. the concatenated compressed blocks.

The same model represents raw and base64 XML storage. Segment readers remove
the storage encoding; the XML binary-block reader validates headers and
decompresses the payload.

## XML Metadata Pass

VTK XML can mix `format="ascii"`, `format="binary"`, and
`format="appended"` arrays in one file. Storage mode is therefore recorded per
array rather than per file.

The streaming metadata tokenizer:

- records inline ASCII byte ranges and token counts without retaining values;
- decodes only enough inline base64 to determine binary-block boundaries;
- stops normal XML tokenization at raw appended bytes;
- locates encoded appended ranges without decoding the complete payload;
- resumes XML parsing after inline payloads so nested metadata remains visible.

For appended raw data, a `DataArray offset` is relative to the first byte after
the `AppendedData` underscore marker.

For appended base64 data, the offset is an **encoded-character offset** relative
to that marker, not a decoded-byte offset. It must preserve base64 quartet
alignment and remain within the encoded appended range. The metadata pass
translates it into absolute encoded source segments before lazy evaluation.

Compressed inline base64 contains separately finalized encodings for the
header/table and compressed-block bytes. Padding between those segments is
valid and must not be treated as one continuous base64 stream.

These offset and segmentation rules are parser contracts. Changing them
requires malformed-input and mixed-storage regression coverage.

## Lazy Array Evaluation

The file-format layer authors all lightweight structure and registers one lazy
time-sample loader for every potentially varying value. A runtime `FileHandle`
contains immutable file context and read options; it does not own a decoded
appended buffer.

Array evaluation supports:

- direct scalar payloads;
- floating scalar storage reinterpreted as two-, three-, or four-component
  vectors;
- XML cell offsets with a leading zero added directly in final storage;
- legacy packed cell arrays preserved in their native packed topology form.

The reader preserves scalar width, signedness, and floating precision whenever
OpenUSD provides a corresponding array type. Unsupported optional arrays warn
and are skipped; malformed required structural arrays fail the read.

Array names are sanitized and made unique deterministically. The original VTK
name remains in field metadata while the authored instance name is used for USD
paths.

## Memory and Parallelism Invariants

Large payloads are materialized directly into their final `VtArray` storage.
When every element will be overwritten, readers use
`MakeUninitializedVtArray()` to avoid an unnecessary zero-fill and extra
full-array pass.

`ioThreads` caps direct file-read fanout. It does not modify the process-wide
OpenUSD Work concurrency limit. Compute operations use OpenUSD Work and write
to disjoint output ranges.

The following choices are deliberate and require representative benchmarks
before being changed:

- compressed XML blocks are read and decompressed in bounded, dynamically
  claimed batches;
- the current compressed path uses a benchmarked 64-block batch baseline;
- compressed data is decompressed directly into final output storage instead
  of first materializing the complete compressed payload;
- base64 range reads decode only small unaligned head and tail groups into
  scratch storage, while aligned middle ranges decode directly into their
  destination slices;
- readers do not retain a complete decoded appended-data buffer.

Past experiments showed that a persistent positioned file reader and alternate
compressed batching strategies did not improve representative hot-cache
workloads. That result matters only as rationale for the current invariants;
new evidence may justify a different implementation.

## Diagnostics

Diagnostics should identify the file, dataset family, array name, association,
role, scalar type, component and tuple counts, storage kind, and source offsets
when available.

- Use warnings for advertised optional arrays that cannot be exposed.
- Use runtime errors or exceptions for malformed required structure.
- Use debug output for cache decisions, storage paths, deterministic renaming,
  derived-array normalization, and copy fallbacks.
- Aggregate payload metrics rather than logging every read or decompression
  task.

## Known Limitations

- Parallel XML wrapper formats such as `.pvtu` are outside the current reader.
- HDF-backed VTK formats need dataset- or hyperslab-addressed payload locations
  rather than the current byte-segment model.
- Integer vector arrays and unsupported component counts are skipped.
- Optional downcast modes are not exposed.

Any new storage family should preserve the separation between parsed metadata,
payload location, lazy evaluation, and USD authoring rather than broadening the
runtime file handle into a general mutable byte store.
