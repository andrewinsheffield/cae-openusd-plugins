# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Direct-pytest bootstrap for the VTKHDF importer tests.

When launched by `cae_add_pytest()`, the CMake test runner sets
`PXR_PLUGINPATH_NAME`, `PYTHONPATH`, and `CAE_VTKHDF_TEST_DATA_DIR` for us and
this file is a no-op.

For manual runs from a shell that already has a pxr Python (e.g. usd-core in
a conda env), set these before invoking pytest::

    set CAE_VTKHDF_PACKAGE_ROOT=<path to extracted cae_openusd_plugins pkg>
    set CAE_VTKHDF_TEST_DATA_DIR=<repo>\\JP_VTKHDF
    pytest tests/python/file_format_vtkhdf

The bootstrap registers the plugin package via
``cae_openusd_plugins.register_usd_plugins()``.
"""

from __future__ import annotations

import os
import pathlib
import sys


def _prepend_dll_dir(path: pathlib.Path) -> None:
    if not path.is_dir():
        return
    try:
        os.add_dll_directory(str(path))
    except (AttributeError, OSError):
        pass


def _bootstrap_kit_usd_runtime() -> None:
    """Ensure `pxr` resolves against kit-cae's split-libs USD when needed."""

    usd_root_env = os.environ.get("CAE_VTKHDF_USD_ROOT")
    if usd_root_env:
        usd_root = pathlib.Path(usd_root_env)
    else:
        usd_root = pathlib.Path(
            r"C:\Users\ahobbs\Documents\OV-Composer\kit-cae-3.0\_build\target-deps\usd\release"
        )

    pxr_path = usd_root / "lib" / "python"
    if not (pxr_path / "pxr").is_dir():
        return

    # If the active pxr is a different one (e.g. usd-core), our schemas will
    # fail to import. Prefer kit's pxr by prepending it to sys.path.
    sys.path.insert(0, str(pxr_path))
    _prepend_dll_dir(usd_root / "bin")
    _prepend_dll_dir(usd_root / "lib")


def _register_local_plugin_override() -> None:
    if os.environ.get("PXR_PLUGINPATH_NAME"):
        return  # a test runner already configured the plugin path

    pkg_root_env = os.environ.get("CAE_VTKHDF_PACKAGE_ROOT")
    if not pkg_root_env:
        return
    pkg_root = pathlib.Path(pkg_root_env)
    if not pkg_root.is_dir():
        return

    lib_python = pkg_root / "lib" / "python"
    plugin_dir = pkg_root / "plugin" / "usd"
    if not lib_python.is_dir() or not plugin_dir.is_dir():
        return

    sys.path.insert(0, str(lib_python))
    _prepend_dll_dir(plugin_dir)
    try:
        import cae_openusd_plugins

        cae_openusd_plugins.register_usd_plugins(strict_version=False)
    except Exception:
        pass


_bootstrap_kit_usd_runtime()
_register_local_plugin_override()
