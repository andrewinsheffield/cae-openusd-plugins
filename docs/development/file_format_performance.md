<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# File Format Performance Patterns

> **Status:** Maintainer guidance for the current reader architecture
>
> **Audience:** File-format developers and reviewers

This document collects implementation patterns for performant CAE file-format
plugins. It is written from the VTK refactor, but the goal is broader: use it
when improving existing readers and when designing a new reader that may expose
large arrays through OpenUSD.

The most important rule is simple: make the cheap pass describe data, and make
the expensive pass happen only when USD asks for a value. Everything else in
this document supports that split.

## Reference Implementations

Use these as worked examples before inventing a new shape:

| Pattern | Reference |
| --- | --- |
| Resolver-backed filename access | [`docs/file_formats/asset_resolution.md`](../file_formats/asset_resolution.md) |
| Layered metadata-to-payload reader | `source/file_formats/vtk/src/` |
| Detailed VTK worked example | [`docs/development/vtk_reader_architecture.md`](vtk_reader_architecture.md) |
| Combined `CaeFileFormatData` backend | `source/file_formats/shared/CaeFileFormatData.*` |
| Caller-filled `VtArray` storage | `source/file_formats/shared/UninitializedVtArray.h` |
| Native C++ lazy readers | `source/file_formats/{vtk,cgns,ensight,openfoam,eclipse}/` |
| Python-backed lazy readers | `source/file_formats/shared/PythonFileFormatBase.*` |
| NumPy-to-USD array conversion | `source/file_formats/numpy/python/cae_{npy,npz}/utils.py` |
| Repeatable schema-aware read benchmark | `tools/benchmarks/` |

## Resolver-Backed Asset Access

Treat every identifier handed to a file-format plugin as an Asset Resolver
identifier, not as a native filesystem path. This applies to all formats,
including formats whose native or Python library accepts only filenames.

Use `source/file_formats/shared/ResolverAsset.*` at the file-format boundary:

1. Keep the original layer identifier for USD composition, root naming,
   diagnostics, and relative child-asset resolution.
2. Open the resolved asset through `ArResolver::OpenAsset()`.
3. Pass `CaeResolverAsset::LocalPath()` to filename-only libraries. The helper
   reuses a resolver-provided backing file when available and otherwise
   materializes the asset into temporary storage.
4. Retain the `CaeResolverAsset` in `CaeFileFormatData` for as long as deferred
   loaders may reopen that path.

Do not add a dependency on a scheme-specific storage client to a file-format
plugin. The host application owns resolver installation, authentication,
caching, and transport policy.

For multi-file formats, resolve each explicit child name with
`CaeResolveSiblingAsset()` relative to the root asset identifier and retain
each returned lease. A deterministic filename pattern is acceptable when it
can be expanded without discovery. Wildcards, timestep discovery, and other
directory scans are different: the Asset Resolver API does not provide
portable directory enumeration. Such layouts must either:

- require a local filesystem and report a clear unsupported-layout error for
  resolver-backed identifiers; or
- define an explicit manifest that names every child asset.

`CanRead()` should localize only the root asset and perform a cheap probe. It
must not fetch every child in a multi-file dataset. `Read()` performs child
resolution once the original root identifier is available.

## 1. Separate Structure Discovery From Value Loading

Large file formats should use one combined backend:

- Build inexpensive structure in an ordinary anonymous layer.
- Register lazy time-sample loaders in `CaeFileFormatData`.
- Copy the structural fields into that backend and install it on the native
  file-format layer.

This keeps structure and values in one layer while preserving deferred I/O.
Every potentially varying value must be registered as time samples, including
single-state values at their transformed source time.

Use `CaeFileFormatData::ParseCacheMode(args)` and pass the selected cache mode into
the backend. Layers should set `timeCodesPerSecond = 1.0`.

## 2. Parse Metadata, Not Values

The parse/index pass should produce immutable metadata that is safe to cache:

- file identity and file-level settings
- source array names, roles, associations, scalar types, tuple counts, and
  component counts
- source locations such as absolute byte ranges, record chunks, member names,
  library object ids, or hyperslab descriptors
- enough diagnostic context to explain skipped or failed arrays later

Do not store decoded heavy buffers, live file handles, open library handles, or
materialized `VtArray` values in parsed metadata.

VTK's `DatasetSpec` is the canonical example for byte-addressable formats:
parsers translate every storage-specific coordinate into absolute source file
segments before lazy loading. Eclipse's binary index is the analogous record
format example: it stores item headers and chunk offsets, then lazy loaders read
the chunks later.

When structure discovery and lazy loaders need the same parsed metadata, cache
immutable specs by resolved path plus a file signature. The cache key
should include only values that change parsing. Arguments such as root prim
path, `cacheMode`, and `ioThreads` should not fork parsed specs unless they
genuinely alter metadata discovery.

For expensive per-file indexes used by many lazy arrays, use single-flight
caching so concurrent misses build the index once and share the result. EnSight
uses a `shared_future` cache for geometry and variable indexes; VTK's
`DatasetSpecCache` uses an in-flight condition variable for the same idea.

## 3. Keep Runtime Context Thin

Separate metadata from runtime file context:

- Metadata specs describe what to load.
- Runtime context carries request-local options and file-level settings needed
  while loading.
- Segment or library readers perform the actual reads.

VTK's `FileHandle` intentionally stores path, byte order, XML header type,
compressor, and read options. It does not own decoded appended buffers or a
large byte-addressable cache. CGNS loaders reopen the source under the CGNS
mutex because the library owns I/O; that is the right equivalent for a
library-backed format.

This separation keeps the metadata cache small and lets lazy loaders be
self-contained closures that can run later, possibly on a different thread.

## 4. Materialize Directly Into The Final USD Value

Every large array path should minimize full-array allocations and full-array
passes:

- Allocate the final `VtArray<T>` once, then read or decode into `data()`.
- If the reader overwrites every byte, avoid `VtArray(size_t)` zero-fill by
  using `PXR_NS::MakeUninitializedVtArray<T>()` from
  `source/file_formats/shared/UninitializedVtArray.h`.
- When source bytes already match the target layout and alignment, foreign-wrap
  the decoded byte storage instead of copying.
- Use small scratch buffers only for conversions that are unavoidable:
  byte-swap, width conversion, component repacking, or planar-to-interleaved
  layout changes.
- Partition work so each task writes a disjoint destination range.

Use the shared uninitialized helper for caller-filled buffers that are fully
populated by a file read, library read, decompressor, decoder, or conversion
kernel before the value is published. The helper returns a foreign-backed
`VtArray<T>` plus a writable sidecar pointer; only use that pointer during the
fill. Do not use this pipeline for non-trivial element types, partial fills, or
arrays where default values are semantically required.

For Python-backed readers, return `Vt.*Array.FromNumpy(np.ascontiguousarray(...))`
for supported dtypes and vector shapes. Avoid `.tolist()` except as a fallback
for unsupported object/string-like values; it turns one bulk conversion into
per-element Python work.

## 5. Preserve Source Types And Source Shape

Prefer the source's native scalar width, signedness, precision, and topology
representation when OpenUSD can carry them:

- `Float64` points should become `double3[]` unless a caller explicitly asks
  for downcast behavior.
- `UInt8` cell types should remain `uchar[]`.
- `Int64` offsets and ids should stay `int64[]` when that is the source type.
- Packed topology should be exposed as packed topology when the source is
  packed, not eagerly transcoded into a different representation.

Only widen or repack when USD has no usable target type or when the source
layout cannot be represented faithfully. Expensive semantic transforms belong
in applications or adapters unless the file-format schema explicitly promises
that normalized representation.

When a reader starts returning a new Sdf value type through `CaeFileFormatData`,
also extend `CaeFileFormatData::_GetFieldTypeid()` so OpenUSD metadata probes do
not trigger the loader.

## 6. Bound Parallelism And Temporary Memory

Treat `ioThreads` as a direct I/O fanout cap, not as a process-wide thread-pool
setting:

- Do not call `WorkSetConcurrencyLimit()` from a file format.
- Direct byte-range reads should use at most `min(ioThreads, workLimit,
  availableChunks)`.
- Compute loops such as decompression, base64 decode, byte-swap, and conversion
  should use `WorkParallelForN()` over their natural work items and let USD
  Work/TBB enforce the active process limit.
- Small reads should stay serial.

Compressed and encoded payloads need bounded staging:

- Decompress compressed blocks in batches directly into the final output
  buffer. Do not first load the full compressed payload when it may be multiple
  gigabytes.
- Decode base64 range requests by handling partial head/tail triples in tiny
  scratch buffers and decoding the aligned middle directly into caller-owned
  target slices.
- Keep benchmarked batch sizes internal. The useful external tuning knob is
  `ioThreads`; payload grain sizes remain implementation details.

## 7. Make Loader And Cache Probes Cheap

Lazy values need cheap metadata queries:

- `CaeFileFormatData` registration records type names up front.
- `QueryTimeSampleTypeid()` and `GetTypeid()` answer from registered metadata,
  not by invoking loaders.
- Time samples should be stored sorted. Use
  `RegisterLazyTimeSamplesSorted()` when the caller has already proven order.
- Loader calls should not hold the Python GIL; `CaeFileFormatData` releases it
  around the callable.

Loader closures should capture immutable descriptors by value: source path,
array metadata, record chunk list, time sample index, and read options. They
should not capture `UsdPrim`, mutable parser state, or a handle whose lifetime
is tied to the structure pass.

## 8. Keep `CanRead()` Cheap

`CanRead()` should answer "could this plugin read this file?" without walking
the whole dataset:

- Check extension, magic bytes, a small file prefix, or minimal library open.
- Avoid parsing the complete metadata tree.
- Avoid reading array payloads.
- Log diagnostics under the format's `TF_DEBUG` symbol, not as warnings for
  ordinary mismatches.

VTK content-sniffs a small prefix and chooses between legacy and XML parsers.
OpenFOAM checks for the required `polyMesh` files. CGNS does a minimal CGIO
open probe.

## 9. Centralize Byte Order, Type Mapping, And Shape Mapping

The parser should normalize source scalar tokens into a small internal scalar
enum before payload loading. Preserve the original source token in diagnostics.

Centralize these decisions:

- source token to internal scalar type
- scalar type to Sdf value type
- byte-order normalization
- scalar/vector shape support
- deterministic USD instance-name sanitization and duplicate handling

That keeps every encoding path from growing its own subtly different type
mapper. VTK's `Types.*`, `ParserUtils.*`, and `ArrayEvaluator.*` split is the
current reference.

## 10. Keep Diagnostics Aggregated And Actionable

Performance code needs diagnostics, but not per-chunk noise:

- Register each `TF_DEBUG` environment symbol in a `TF_REGISTRY_FUNCTION`.
- Log high-level branch decisions: mode, arguments, cache hit/miss, storage
  path, array registration, and aggregate load metrics.
- Use `TF_WARN` for arrays the source advertises but the plugin cannot expose.
- Use `TF_RUNTIME_ERROR` or exceptions for malformed required structural data.
- Include file path, source array name, authored array name, association, role,
  scalar type, component count, tuple count, storage kind, and source offsets
  when available.

For VTK-style chunked readers, prefer one aggregate line per array or payload
load. Per-task begin/end logs are usually too noisy to help.

## 11. Benchmark Before And After Changing Invariants

Before changing a performance-sensitive reader, capture a baseline that
separates:

- layer open/structure time
- lazy `UsdAttribute::Get()` time
- explicit value release time when large arrays are involved
- cache mode (`all`, `static`, `none`)
- thread count sweep, usually `1,2,4,8,16`
- hot-cache vs cold-cache expectations
- peak memory or obvious extra full-payload allocation when that is the risk

Run each plugin/thread sample in a fresh process when possible so plugin load,
OpenUSD caches, and file-format metadata caches do not bleed across samples.
The standalone project under `tools/benchmarks/` implements this measurement
shape across the supported file-format schemas while preserving native
topology representations.

## 12. Share Runtime Utilities Only After The Interface Settles

The shared runtime utilities with broad reach belong in `source/file_formats/shared/`.
`CaeFileFormatData` and `PythonFileFormatBase` are examples: many plugins depend on
them and duplicated copies would be expensive.

Keep narrow parser and materialization helpers local until several readers need
the same interface. Chunked direct-materialization helpers still vary by
format: byte ranges, record chunks, endian policy, library callbacks, component
repacking, and internal grain sizes are not identical. Copy the proven local
shape for a new binary-bearing reader, then revisit sharing when the signature
has enough consumers to be stable.

## New Reader Checklist

Before implementing a new file format, answer these:

- What is the cheapest metadata/index pass, and what exact descriptors does it
  produce for each heavy array?
- Which arrays can be lazy, and which tiny values must be authored eagerly?
- Does the format need one plugin for multiple envelopes that produce the same
  USD shape, as VTK does, or separate file-format classes with distinct
  argument surfaces?
- Which format arguments affect parsing, and which affect only read-time
  materialization?
- Which source scalar and vector types can be preserved directly in USD?
- Where are byte order, decompression, and component layout normalized?
- Can loaders fill final `VtArray` storage directly?
- What metadata/index cache is safe, how is it invalidated, and does it need
  single-flight behavior?
- Which operations should use `ioThreads`, and which should rely only on USD
  Work concurrency?
- What benchmark will catch regressions before the change lands?

## Existing Reader Retrofit Checklist

When improving an existing file format, look for these smells:

- `Read()` repeats structure discovery or lazy registration work.
- Metadata parsing materializes arrays only to discover counts, offsets, or
  names.
- A large binary path reads into `std::vector`, converts into another
  `std::vector`, then copies into `VtArray`.
- Python readers return `.tolist()` for numeric arrays.
- Time sample registration sorts a list that was already known to be sorted.
- Type probes or `GetTypeid()` paths invoke heavy loaders.
- `CanRead()` performs a full parse.
- Debug output logs every chunk instead of aggregate array/load metrics.
- Cache keys include options that do not affect parsing.
- The file-format layer eagerly normalizes a source representation that a
  consumer could decode lazily.

Fixing these usually gives larger wins than micro-optimizing token parsing or
reusing a single file descriptor.
