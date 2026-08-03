# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Integration tests for the PythonProxyFileFormat (.pydf) plugin.

These tests verify the full round-trip through ``PythonFileFormatBase``:

- Plugin registration via the USD PlugRegistry.
- Stage open with ``pythonModule`` / ``pythonPath`` format arguments.
- Combined structure and lazy values through ``CaeFileFormatData``.
- Single-state lazy array attributes sampled at time 0.
- Time-sampled lazy array attributes.

The Python reader under test is :mod:`python_proxy_reader` (same directory).
All tests are marked ``integration`` and require the plugin shared library to
be on ``LD_LIBRARY_PATH`` / ``PXR_PLUGINPATH_NAME``.
"""

import importlib
import pathlib
import sys

import pytest
from pxr import Sdf, Usd


def _fixture_dir() -> pathlib.Path:
    return pathlib.Path(__file__).parent


def _data_file() -> pathlib.Path:
    return _fixture_dir() / "sample.pydf"


def _layer_identifier(**extra_args) -> str:
    """Build the layer identifier for sample.pydf with the test reader wired in."""
    fixture_dir = _fixture_dir()
    args = {
        "pythonModule": "python_proxy_reader",
        "pythonPath": str(fixture_dir),
    }
    args.update(extra_args)
    return Sdf.Layer.CreateIdentifier(
        str(_data_file()),
        args,
    )


def _plugin_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciPythonProxyFileFormat") is not None


def _reader_module():
    fixture_dir = str(_fixture_dir())
    if fixture_dir not in sys.path:
        sys.path.insert(0, fixture_dir)
    return importlib.import_module("python_proxy_reader")


@pytest.mark.integration
def test_plugin_registered():
    assert _plugin_available(), "OmniSciPythonProxyFileFormat not registered"


@pytest.mark.integration
def test_python_proxy_stage_opens():
    stage = Usd.Stage.Open(_layer_identifier())
    assert stage, "Failed to open .pydf stage through Python proxy file format"

    root = stage.GetDefaultPrim()
    assert root
    assert root.GetPath() == Sdf.Path("/data")


@pytest.mark.integration
def test_lazy_single_state_array_value_requires_explicit_time():
    """A single-state lazy attribute is sampled and has no default value."""
    stage = Usd.Stage.Open(_layer_identifier())
    prim = stage.GetPrimAtPath("/data")
    assert prim

    attr = prim.GetAttribute("values")
    assert attr.Get() is None
    assert attr.GetTimeSamples() == [0.0]
    values = attr.Get(Usd.TimeCode.EarliestTime())
    assert list(values) == [1.0, 2.0, 3.5]


@pytest.mark.integration
def test_lazy_time_samples():
    """Time-sampled lazy attribute returns the correct sample at each time code."""
    stage = Usd.Stage.Open(_layer_identifier())
    prim = stage.GetPrimAtPath("/data")
    assert prim

    series = prim.GetAttribute("series")
    assert series
    assert list(series.Get(0.0)) == [0.0, 0.5]
    assert list(series.Get(1.0)) == [1.0, 1.5]


@pytest.mark.integration
def test_cache_mode_all_caches_single_state_and_time_samples():
    reader = _reader_module()
    reader.reset_load_counts()

    stage = Usd.Stage.Open(_layer_identifier(cacheMode="all"))
    prim = stage.GetPrimAtPath("/data")

    values = prim.GetAttribute("values")
    series = prim.GetAttribute("series")
    assert list(values.Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.5]
    assert list(values.Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.5]
    assert list(series.Get(0.0)) == [0.0, 0.5]
    assert reader.get_load_count("series:0") == 1
    assert list(series.Get(0.0)) == [0.0, 0.5]

    assert reader.get_load_count("values") == 1
    assert reader.get_load_count("series:0") == 1


@pytest.mark.integration
def test_cache_mode_static_does_not_cache_sampled_values():
    reader = _reader_module()
    reader.reset_load_counts()

    stage = Usd.Stage.Open(_layer_identifier(cacheMode="static"))
    prim = stage.GetPrimAtPath("/data")

    values = prim.GetAttribute("values")
    series = prim.GetAttribute("series")
    assert list(values.Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.5]
    assert list(values.Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.5]
    assert list(series.Get(0.0)) == [0.0, 0.5]
    series_loads_after_first_get = reader.get_load_count("series:0")
    assert series_loads_after_first_get == 1
    assert list(series.Get(0.0)) == [0.0, 0.5]

    assert reader.get_load_count("values") == 2
    assert reader.get_load_count("series:0") == series_loads_after_first_get + 1


@pytest.mark.integration
def test_cache_mode_none_does_not_cache_values():
    reader = _reader_module()
    reader.reset_load_counts()

    stage = Usd.Stage.Open(_layer_identifier(cacheMode="none"))
    prim = stage.GetPrimAtPath("/data")

    values = prim.GetAttribute("values")
    series = prim.GetAttribute("series")
    assert list(values.Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.5]
    value_loads_after_first_get = reader.get_load_count("values")
    assert list(values.Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.5]
    assert list(series.Get(0.0)) == [0.0, 0.5]
    series_loads_after_first_get = reader.get_load_count("series:0")
    assert series_loads_after_first_get == 1
    assert list(series.Get(0.0)) == [0.0, 0.5]

    assert reader.get_load_count("values") > value_loads_after_first_get
    assert reader.get_load_count("series:0") == series_loads_after_first_get + 1
