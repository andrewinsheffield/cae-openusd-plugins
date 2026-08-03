# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""
Shared pytest fixtures for CAE USD plugin tests.

Plugin discovery
----------------
When run via ctest, PXR_PLUGINPATH_NAME is set in the environment before
Python starts, so USD discovers our plugins automatically.

When running pytest directly against an install tree, set one of:
  PXR_PLUGINPATH_NAME=<install>/plugin/usd pytest tests/python/
  CAE_INSTALL_DIR=<install>               pytest tests/python/

When running against a wheel, install the wheel and let
``cae_openusd_plugins.register_usd_plugins()`` register the packaged plugin tree.

Test data
---------
The DATA_DIR fixture resolves to tests/data/ relative to this file.
"""

import os
import pathlib

import pytest
from pxr import Plug, Usd

# ---------------------------------------------------------------------------
# Plugin registration
# ---------------------------------------------------------------------------
def pytest_configure(config):
    """Register CAE plugins before test modules import schema packages."""
    try:
        import cae_openusd_plugins
    except ModuleNotFoundError:
        cae_openusd_plugins = None

    if cae_openusd_plugins is not None and cae_openusd_plugins.usd_plugin_path().is_dir():
        cae_openusd_plugins.register_usd_plugins()
        return

    install_dir = os.environ.get("CAE_INSTALL_DIR")
    if install_dir:
        plugin_dir = os.path.join(install_dir, "plugin", "usd")
        Plug.Registry().RegisterPlugins(plugin_dir)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------
@pytest.fixture
def stage():
    """Fresh anonymous in-memory USD stage, discarded after each test."""
    return Usd.Stage.CreateInMemory()


@pytest.fixture(scope="session")
def data_dir():
    """Path to tests/data/ -- holds .usda round-trip fixtures."""
    return pathlib.Path(__file__).parent.parent / "data"
