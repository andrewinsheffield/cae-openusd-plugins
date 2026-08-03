# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib

import numpy as np
import pytest
from pxr import Sdf, Usd


def _plugin_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciNpyFileFormat") is not None


def _identifier(path: pathlib.Path, **kwargs) -> str:
    return Sdf.Layer.CreateIdentifier(str(path), kwargs)


def _npy_structure_layers(stage: Usd.Stage):
    npy_format = Sdf.FileFormat.FindById("OmniSciNpyFileFormat")
    return [
        layer
        for layer in stage.GetUsedLayers()
        if layer.GetFileFormat() == npy_format
    ]


@pytest.fixture
def require_npy_plugin():
    if not _plugin_available():
        pytest.skip("OmniSciNpyFileFormat plugin not registered")


@pytest.fixture
def npy_path(tmp_path: pathlib.Path) -> pathlib.Path:
    path = tmp_path / "values.npy"
    np.save(path, np.array([1.25, 2.5, 3.75], dtype=np.float64))
    return path


@pytest.mark.integration
def test_plugin_registered():
    assert _plugin_available(), "OmniSciNpyFileFormat not registered"


@pytest.mark.integration
def test_payload_attribute_sugar_sets_dynamic_arguments(require_npy_plugin, npy_path: pathlib.Path):
    OmniSciFileFormatArgs = pytest.importorskip(
        "pxr.OmniSciFileFormatArgs",
        reason="omniSciFileFormatArgs plugin not available",
    )

    stage = Usd.Stage.CreateInMemory()
    prim = stage.DefinePrim("/Npy")
    npy_api = OmniSciFileFormatArgs.NpyAPI.Apply(prim)
    npy_api.CreateArrayNameAttr().Set("temperature")
    prim.GetPayloads().AddPayload(str(npy_path))
    stage.Load(prim.GetPath())

    assert prim.HasAPI(OmniSciFileFormatArgs.NpyAPI)

    layers = _npy_structure_layers(stage)
    assert len(layers) == 1
    assert layers[0].GetFileFormatArguments().get("arrayName") == "temperature"

    values = prim.GetAttribute("omni:sci:array:temperature:value").Get(Usd.TimeCode.EarliestTime())
    assert list(values) == [1.25, 2.5, 3.75]


@pytest.mark.integration
def test_npy_opens_as_single_raw_array(require_npy_plugin, npy_path: pathlib.Path):
    stage = Usd.Stage.Open(_identifier(npy_path))
    assert stage

    dataset = stage.GetPrimAtPath("/values")
    assert dataset
    assert dataset.GetTypeName() == "OmniSciDataset"

    api_schemas = set(dataset.GetAppliedSchemas())
    assert "OmniSciArrayAPI:array" in api_schemas
    assert "OmniSciCaePointCloudAPI" not in api_schemas
    assert not any(schema.startswith("OmniSciFieldAPI") for schema in api_schemas)

    values = dataset.GetAttribute("omni:sci:array:array:value").Get(Usd.TimeCode.EarliestTime())
    assert list(values) == [1.25, 2.5, 3.75]


@pytest.mark.integration
def test_npy_values_are_time_sampled(require_npy_plugin, npy_path: pathlib.Path):
    stage = Usd.Stage.Open(_identifier(npy_path))
    assert stage

    dataset = stage.GetPrimAtPath("/values")
    values = dataset.GetAttribute("omni:sci:array:array:value")
    assert values.GetTimeSamples() == [0.0]
    assert list(values.Get(Usd.TimeCode(0))) == [1.25, 2.5, 3.75]

    assert dataset.GetAttribute("omni:sci:array:array:device").Get() == "cpu"


@pytest.mark.integration
def test_array_name_argument_overrides_instance_name(require_npy_plugin, npy_path: pathlib.Path):
    stage = Usd.Stage.Open(_identifier(npy_path, arrayName="temperature"))
    assert stage

    dataset = stage.GetPrimAtPath("/values")
    api_schemas = set(dataset.GetAppliedSchemas())
    assert "OmniSciArrayAPI:temperature" in api_schemas
    assert "OmniSciArrayAPI:array" not in api_schemas

    values = dataset.GetAttribute("omni:sci:array:temperature:value").Get(Usd.TimeCode.EarliestTime())
    assert list(values) == [1.25, 2.5, 3.75]


@pytest.mark.integration
def test_schema_argument_is_ignored_for_npy(require_npy_plugin, npy_path: pathlib.Path):
    stage = Usd.Stage.Open(_identifier(npy_path, schema="Point Cloud"))
    assert stage

    dataset = stage.GetPrimAtPath("/values")
    api_schemas = set(dataset.GetAppliedSchemas())
    assert "OmniSciArrayAPI:array" in api_schemas
    assert "OmniSciCaePointCloudAPI" not in api_schemas


@pytest.mark.integration
def test_npy_with_trailing_vec3_dimension_uses_float3_array(require_npy_plugin, tmp_path: pathlib.Path):
    path = tmp_path / "points.npy"
    np.save(path, np.array([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]], dtype=np.float32))

    stage = Usd.Stage.Open(_identifier(path))
    assert stage

    dataset = stage.GetPrimAtPath("/points")
    values = dataset.GetAttribute("omni:sci:array:array:value").Get(Usd.TimeCode.EarliestTime())
    assert str(dataset.GetAttribute("omni:sci:array:array:value").GetTypeName()) == "float3[]"
    assert [tuple(value) for value in values] == [(1.0, 2.0, 3.0), (4.0, 5.0, 6.0)]


@pytest.mark.integration
def test_multidimensional_npy_without_vector_shape_is_flattened(require_npy_plugin, tmp_path: pathlib.Path):
    path = tmp_path / "matrix.npy"
    np.save(path, np.array([[1, 2, 3, 4, 5], [6, 7, 8, 9, 10]], dtype=np.int32))

    stage = Usd.Stage.Open(_identifier(path))
    assert stage

    dataset = stage.GetPrimAtPath("/matrix")
    values = dataset.GetAttribute("omni:sci:array:array:value").Get(Usd.TimeCode.EarliestTime())
    assert str(dataset.GetAttribute("omni:sci:array:array:value").GetTypeName()) == "int[]"
    assert list(values) == [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
