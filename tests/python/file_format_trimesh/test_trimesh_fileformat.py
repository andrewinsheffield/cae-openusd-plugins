# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciCae as OmniSciCae


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "Trimesh"
_STL_PATH = _DATA_DIR / "minimal.stl"


def _format_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciTrimeshFileFormat") is not None


def _identifier(path: pathlib.Path, **kwargs) -> str:
    return Sdf.Layer.CreateIdentifier(str(path), kwargs)


@pytest.mark.integration
def test_plugin_registered():
    assert _format_available(), "OmniSciTrimeshFileFormat not registered"
    assert Sdf.FileFormat.FindByExtension("stl") is not None
    assert Sdf.FileFormat.FindByExtension("ply") is not None
    assert Sdf.FileFormat.FindByExtension("3mf") is not None


@pytest.mark.integration
def test_trimesh_stage_structure_loads_without_trimesh_dependency():
    if not _format_available():
        pytest.skip("OmniSciTrimeshFileFormat file format not registered")

    stage = Usd.Stage.Open(str(_STL_PATH))
    assert stage

    prim = stage.GetDefaultPrim()
    assert prim
    assert prim.GetPath() == Sdf.Path("/minimal")
    assert prim.IsA(OmniSci.Dataset)
    assert prim.HasAPI(OmniSciCae.MeshAPI)

    assert str(prim.GetAttribute("omni:sci:array:points:value").GetTypeName()) == "float3[]"
    assert str(prim.GetAttribute("omni:sci:array:faceVertexIndices:value").GetTypeName()) == "int[]"
    assert str(prim.GetAttribute("omni:sci:array:faceVertexCounts:value").GetTypeName()) == "int[]"


@pytest.mark.integration
def test_trimesh_values_are_time_sampled():
    if not _format_available():
        pytest.skip("OmniSciTrimeshFileFormat file format not registered")

    stage = Usd.Stage.Open(_identifier(_STL_PATH))
    assert stage
    prim = stage.GetPrimAtPath("/minimal")
    assert prim

    points = prim.GetAttribute("omni:sci:array:points:value")
    indices = prim.GetAttribute("omni:sci:array:faceVertexIndices:value")
    counts = prim.GetAttribute("omni:sci:array:faceVertexCounts:value")

    assert points.GetTimeSamples() == [0.0]
    assert indices.GetTimeSamples() == [0.0]
    assert counts.GetTimeSamples() == [0.0]
    assert str(points.GetTypeName()) == "float3[]"
    assert str(indices.GetTypeName()) == "int[]"
    assert str(counts.GetTypeName()) == "int[]"
    assert prim.GetAttribute("omni:sci:array:points:device").Get() == "cpu"


@pytest.mark.integration
def test_trimesh_lazy_values_when_dependency_available():
    if not _format_available():
        pytest.skip("OmniSciTrimeshFileFormat file format not registered")
    if importlib.util.find_spec("trimesh") is None:
        pytest.skip("trimesh Python package is not installed")

    stage = Usd.Stage.Open(str(_STL_PATH))
    prim = stage.GetDefaultPrim()

    points = prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    indices = prim.GetAttribute("omni:sci:array:faceVertexIndices:value").Get(Usd.TimeCode.EarliestTime())
    counts = prim.GetAttribute("omni:sci:array:faceVertexCounts:value").Get(Usd.TimeCode.EarliestTime())

    assert len(points) == 3
    assert list(indices) == [0, 1, 2]
    assert list(counts) == [3]
