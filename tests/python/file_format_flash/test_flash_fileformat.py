# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import os
import pathlib

import pytest
from pxr import Sdf, Tf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciFlash as OmniSciFlash


_DATA_DIR_ENV = os.environ.get("CAE_FLASH_TEST_DATA_DIR")
_DATA_DIR = pathlib.Path(_DATA_DIR_ENV) if _DATA_DIR_ENV else None
_SINGLE = _DATA_DIR / "minimal.flash" if _DATA_DIR else None
_SERIES = _DATA_DIR / "series.flash" if _DATA_DIR else None


def _available():
    return Sdf.FileFormat.FindById("OmniSciFlashFileFormat") is not None


def _require_test_data(path):
    if path is None or not path.exists():
        pytest.skip("generated FLASH test data is not available")


@pytest.fixture(scope="module")
def single_stage():
    _require_test_data(_SINGLE)
    stage = Usd.Stage.Open(str(_SINGLE))
    assert stage
    return stage


@pytest.fixture(scope="module")
def series_stage():
    _require_test_data(_SERIES)
    stage = Usd.Stage.Open(str(_SERIES))
    assert stage
    return stage


@pytest.mark.integration
def test_plugin_registered():
    assert _available()


@pytest.mark.integration
def test_single_snapshot_structure(single_stage):
    prim = single_stage.GetDefaultPrim()
    assert prim.GetPath() == Sdf.Path("/minimal")
    assert prim.IsA(OmniSci.Dataset)
    assert prim.HasAPI(OmniSciFlash.AmrAPI)

    api = OmniSciFlash.AmrAPI(prim)
    assert api.GetSpatialDimensionAttr().Get(Usd.TimeCode.EarliestTime()) == 2
    assert list(api.GetGidShapeAttr().Get(Usd.TimeCode.EarliestTime())) == [9]
    assert list(api.GetBoundingBoxShapeAttr().Get(Usd.TimeCode.EarliestTime())) == [3, 2]
    assert list(api.GetCoordinatesShapeAttr().Get(Usd.TimeCode.EarliestTime())) == [3]
    assert list(api.GetFieldShapeAttr().Get(Usd.TimeCode.EarliestTime())) == [1, 2, 2]
    assert not api.GetBlockSizeShapeAttr().HasAuthoredValue()


@pytest.mark.integration
def test_source_order_topology_and_fields_are_complete(single_stage):
    prim = single_stage.GetDefaultPrim()
    node_types = prim.GetAttribute("omni:sci:array:nodeType:value").Get(Usd.TimeCode.EarliestTime())
    gid = prim.GetAttribute("omni:sci:array:gid:value").Get(Usd.TimeCode.EarliestTime())
    bounds = prim.GetAttribute("omni:sci:array:boundingBox:value").Get(Usd.TimeCode.EarliestTime())
    density = prim.GetAttribute("omni:sci:array:dens:value").Get(Usd.TimeCode.EarliestTime())

    assert list(node_types) == [2, 1, 1]
    assert len(gid) == 3 * 9
    assert list(gid[5:7]) == [2, 3]
    assert len(bounds) == 3 * 3 * 2
    assert list(bounds[:6]) == pytest.approx([0.0, 0.5, 0.0, 1.0, 0.0, 0.1])
    assert len(density) == 3 * 1 * 2 * 2
    assert list(density) == pytest.approx([float(i) for i in range(1, 13)])


@pytest.mark.integration
def test_fields_preserve_lookup_names_and_association(single_stage):
    prim = single_stage.GetDefaultPrim()
    for instance, source in [("dens", "dens"), ("velx", "velx")]:
        assert prim.HasAPI(OmniSci.FieldAPI, instance)
        api = OmniSci.FieldAPI(prim, instance)
        assert api.GetNameAttr().Get(Usd.TimeCode.EarliestTime()) == source
        assert api.GetAssociationAttr().Get(Usd.TimeCode.EarliestTime()) == "element"


@pytest.mark.integration
def test_scalar_and_sim_info_custom_attributes(single_stage):
    prim = single_stage.GetDefaultPrim()
    nxb = prim.GetAttribute("omni:flash:scalar:nxb")
    time = prim.GetAttribute("omni:flash:scalar:time")
    version = prim.GetAttribute("omni:flash:simInfo:file_format_version")
    flash_version = prim.GetAttribute("omni:flash:simInfo:flash_version")

    assert nxb.GetTypeName() == Sdf.ValueTypeNames.Int
    assert nxb.Get(Usd.TimeCode.EarliestTime()) == 2
    assert time.GetTypeName() == Sdf.ValueTypeNames.Double
    assert time.Get(Usd.TimeCode.EarliestTime()) == pytest.approx(0.0)
    assert version.Get(Usd.TimeCode.EarliestTime()) == 9
    assert version.GetCustomDataByKey("flashSourceName") == "file format version"
    assert flash_version.Get(Usd.TimeCode.EarliestTime()) == "FLASH-X fixture"


@pytest.mark.integration
def test_optional_arrays_are_exposed_only_when_present(single_stage):
    prim = single_stage.GetDefaultPrim()
    assert prim.GetAttribute("omni:sci:array:coordinates:value").IsValid()
    assert prim.GetAttribute("omni:sci:array:processorNumber:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:blockSize:value").IsValid()


@pytest.mark.integration
def test_series_uses_time_steps_and_allows_topology_changes(series_stage):
    prim = series_stage.GetDefaultPrim()
    density = prim.GetAttribute("omni:sci:array:dens:value")
    node_types = prim.GetAttribute("omni:sci:array:nodeType:value")

    assert density.GetTimeSamples() == pytest.approx([0.0, 1.0])
    assert len(density.Get(Usd.TimeCode(0.0))) == 12
    assert len(density.Get(Usd.TimeCode(1.0))) == 16
    assert len(node_types.Get(Usd.TimeCode(0.0))) == 3
    assert len(node_types.Get(Usd.TimeCode(1.0))) == 4
    assert density.Get(Usd.TimeCode(1.0))[0] == pytest.approx(101.0)


@pytest.mark.integration
def test_series_scalars_are_time_sampled(series_stage):
    prim = series_stage.GetDefaultPrim()
    blocks = prim.GetAttribute("omni:flash:scalar:globalnumblocks")
    step = prim.GetAttribute("omni:flash:scalar:nstep")
    nxb = prim.GetAttribute("omni:flash:scalar:nxb")

    assert blocks.Get(Usd.TimeCode(0.0)) == 3
    assert blocks.Get(Usd.TimeCode(1.0)) == 4
    assert step.Get(Usd.TimeCode(0.0)) == 10
    assert step.Get(Usd.TimeCode(1.0)) == 20
    assert nxb.GetTimeSamples() == [0.0, 1.0]
    assert nxb.Get(Usd.TimeCode(0.0)) == 2
    assert nxb.Get(Usd.TimeCode(1.0)) == 2
    assert nxb.ValueMightBeTimeVarying()


@pytest.mark.integration
def test_physical_time_override():
    _require_test_data(_SERIES)
    identifier = Sdf.Layer.CreateIdentifier(str(_SERIES), {"timeSource": "TimeValue"})
    stage = Usd.Stage.Open(identifier)
    assert stage
    density = stage.GetDefaultPrim().GetAttribute("omni:sci:array:dens:value")
    assert density.GetTimeSamples() == pytest.approx([0.0, 0.5])


@pytest.mark.integration
def test_negative_time_scale_keeps_lazy_samples_sorted():
    _require_test_data(_SERIES)
    identifier = Sdf.Layer.CreateIdentifier(
        str(_SERIES), {"timeScale": "-2", "timeOffset": "1"}
    )
    stage = Usd.Stage.Open(identifier)
    assert stage
    density = stage.GetDefaultPrim().GetAttribute("omni:sci:array:dens:value")
    assert density.GetTimeSamples() == pytest.approx([-1.0, 1.0])
    assert density.Get(Usd.TimeCode(-1.0))[0] == pytest.approx(101.0)
    assert density.Get(Usd.TimeCode(1.0))[0] == pytest.approx(1.0)


@pytest.mark.integration
def test_non_finite_time_argument_is_rejected():
    _require_test_data(_SERIES)
    identifier = Sdf.Layer.CreateIdentifier(str(_SERIES), {"timeScale": "nan"})
    with pytest.raises(Tf.ErrorException):
        Usd.Stage.Open(identifier)


@pytest.mark.integration
def test_mount_path():
    _require_test_data(_SINGLE)
    identifier = Sdf.Layer.CreateIdentifier(str(_SINGLE), {"mountPath": "/World/Flash"})
    stage = Usd.Stage.Open(identifier)
    assert stage.GetPrimAtPath("/World/Flash")
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/World")
