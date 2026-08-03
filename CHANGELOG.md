<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Changelog

All notable changes to CAE OpenUSD Plugins are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Python package versions use the PEP 440 spelling of development releases, such
as `0.1.0.dev0`.

Add changes to `Unreleased` under `Added`, `Changed`, `Deprecated`, `Removed`,
`Fixed`, or `Security`. At release time, move those entries into a dated version
section and create a fresh `Unreleased` section.

## Unreleased

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
