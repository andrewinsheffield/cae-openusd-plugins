<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Changelog

All notable changes to CAE OpenUSD Plugins are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Python package versions use the PEP 440 spelling of development releases, such
as `0.1.1.dev0`.

Add changes to `Unreleased` under `Added`, `Changed`, `Deprecated`, `Removed`,
`Fixed`, or `Security`. At release time, move those entries into a dated version
section and create a fresh `Unreleased` section.

## Unreleased

### Added

- **VTKHDF DEM particle-series file-format plugin (`.vtkhdf`).** New
  `omniSciVtkHdfFileFormat` reads DEM simulations exported as VTKHDF v2.5
  MultiBlockDataSets with time-varying particle clouds, static-topology
  geometry meshes, and prototype particle templates. Discovers sibling
  `<stem>_t_<N>.vtkhdf` files as automatic time samples and, when present, a
  sibling `particle_templates/` directory for prototype meshes. Reuses the
  EDEM domain schemas (`OmniSciEdemParticleTypeAPI`,
  `OmniSciEdemParticleCloudAPI`, `OmniSciEdemGeometryGroupAPI`) since the DEM
  data model matches. Enabled with `-DCAE_ENABLE_VTKHDF=ON` (default off).
- `OmniSciFileFormatArgsVtkHdfAPI` payload-arguments schema so kit-cae and
  other clients can carry `cacheMode`, `timeScale`, `timeOffset`,
  `timeSource`, `ioThreads`, and `mountPath` on a `.vtkhdf` payload prim.
- Integration test suite `tests/python/file_format_vtkhdf/` (19 tests)
  covering plugin registration, mount-path stripping across a
  `<stem>_t_<N>.vtkhdf` series, prototype meshes, particle-cloud datasets
  with per-vertex fields, time-varying positions, and static geometry meshes.

## [0.1.1] - 2026-08-03

### Fixed

- Enable HDF5 zlib filter support in superbuild-generated dependency SDKs,
  including static Windows consumers.
- Preserve vector-valued fields in NPZ point-cloud datasets.

## [0.1.0] - 2026-08-03

### Added

- Read-only OpenUSD schemas and lazy file-format plugins for EnSight Gold,
  OpenFOAM, Eclipse reservoir data, VTK, CGNS, EDEM, FLASH AMR, NumPy,
  Trimesh, NanoVDB, and custom Python adapters.
- Format-independent and domain schemas for scientific datasets, fields,
  arrays, CAE meshes, and source-specific metadata.
- Resolver-backed native asset access, file-series composition, and USD value
  clips without converting source data.
- CMake install trees, CPack archives, and Python wheels with runtime
  compatibility checks and packaged third-party license notices.
