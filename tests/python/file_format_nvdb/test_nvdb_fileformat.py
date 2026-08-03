# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "NVDB"
_NVDB_PATH = _DATA_DIR / "minimal.nvdb"


def _format_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciNvdbFileFormat") is not None


def _identifier(path: pathlib.Path, **kwargs) -> str:
    return Sdf.Layer.CreateIdentifier(str(path), kwargs)


@pytest.mark.integration
def test_plugin_registered():
    assert _format_available(), "OmniSciNvdbFileFormat not registered"


@pytest.mark.integration
def test_nvdb_stage_structure_loads():
    if not _format_available():
        pytest.skip("OmniSciNvdbFileFormat file format not registered")

    stage = Usd.Stage.Open(str(_NVDB_PATH))
    assert stage

    prim = stage.GetDefaultPrim()
    assert prim
    assert prim.GetPath() == Sdf.Path("/minimal")
    assert prim.IsA(OmniSci.Dataset)

    attr = prim.GetAttribute("omni:sci:array:nanovdb:value")
    assert attr
    assert str(attr.GetTypeName()) == "uint[]"


@pytest.mark.integration
def test_nvdb_values_are_time_sampled():
    if not _format_available():
        pytest.skip("OmniSciNvdbFileFormat file format not registered")

    stage = Usd.Stage.Open(_identifier(_NVDB_PATH))
    assert stage

    prim = stage.GetPrimAtPath("/minimal")
    assert prim
    attr = prim.GetAttribute("omni:sci:array:nanovdb:value")

    assert attr.GetTimeSamples() == [0.0]
    assert str(attr.GetTypeName()) == "uint[]"
    assert prim.GetAttribute("omni:sci:array:nanovdb:device").Get() == "cpu"


@pytest.mark.integration
def test_nvdb_requires_warp_dependency():
    if not _format_available():
        pytest.skip("OmniSciNvdbFileFormat file format not registered")

    assert importlib.util.find_spec("warp") is not None, (
        "omniSciNvdbFileFormat requires warp-lang"
    )
