# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciReservoir as OmniSciReservoir


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "reservoir" / "10_results"
_UNRST = _DATA_DIR / "RESULTS.UNRST"


def _unrst_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciUnrstFileFormat") is not None


def _open_unrst_stage(args=None):
    if not _UNRST.exists():
        pytest.skip(f"UNRST fixture not found: {_UNRST}")
    if not _unrst_available():
        pytest.skip("OmniSciUnrstFileFormat plugin not registered")

    identifier = str(_UNRST) if args is None else Sdf.Layer.CreateIdentifier(str(_UNRST), args)
    stage = Usd.Stage.Open(identifier)
    assert stage
    return stage


@pytest.mark.integration
def test_plugin_registered():
    assert _unrst_available(), "OmniSciUnrstFileFormat not registered"


# UNRST timecodes are canonical simulation seconds. The fixture
# carries two restarts whose DOUBHEAD[0] values are 0 and 12.5 simulation days,
# so the default `(source=TimeValue, scale=86400)` defaults produce timecodes
# at 0 and 12.5 * 86400 = 1_080_000 seconds.
_SECONDS_PER_DAY = 86400.0
_DEFAULT_END_TIMECODE = 12.5 * _SECONDS_PER_DAY


@pytest.mark.integration
def test_unrst_whitelist_time_samples_default_to_simulation_seconds():
    stage = _open_unrst_stage()
    prim = stage.GetDefaultPrim()
    assert prim
    assert prim.GetPath() == Sdf.Path("/RESULTS")
    assert prim.IsA(OmniSci.Dataset)
    assert not prim.HasAPI(OmniSciReservoir.CornerPointGridAPI)

    # Every time-aware plugin's emitted layer self-describes its canonical
    # simulation-seconds unit by setting timeCodesPerSecond = 1.0.
    assert stage.GetTimeCodesPerSecond() == pytest.approx(1.0)

    assert stage.GetStartTimeCode() == pytest.approx(0.0)
    assert stage.GetEndTimeCode() == pytest.approx(_DEFAULT_END_TIMECODE)

    assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "PRESSURE")
    assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "SWAT")
    assert not prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "CUSTOM")

    pressure_api = OmniSciReservoir.CellPropertyAPI(prim, "PRESSURE")
    assert pressure_api.GetIndexSpaceAttr().Get(Usd.TimeCode.EarliestTime()) == "activeCells"
    assert pressure_api.GetSourceKeywordAttr().Get(Usd.TimeCode.EarliestTime()) == "PRESSURE"

    pressure = prim.GetAttribute("omni:sci:array:PRESSURE:value")
    assert pressure.GetTimeSamples() == pytest.approx([0.0, _DEFAULT_END_TIMECODE])
    assert list(pressure.Get(0.0)) == pytest.approx([100.0, 101.0, 102.0])
    assert list(pressure.Get(_DEFAULT_END_TIMECODE)) == pytest.approx([110.0, 111.0, 112.0])


@pytest.mark.integration
def test_unrst_iteration_value_picks_seqnum():
    """Legacy `unrstTimeAxis=reportStep` is now expressed as
    `timeSource=IterationValue` with `timeScale=1.0` so the SEQNUM counter is
    not multiplied by the default seconds-per-day."""
    stage = _open_unrst_stage({"timeSource": "IterationValue", "timeScale": "1.0"})
    prim = stage.GetDefaultPrim()
    pressure = prim.GetAttribute("omni:sci:array:PRESSURE:value")

    assert stage.GetTimeCodesPerSecond() == pytest.approx(1.0)
    assert stage.GetStartTimeCode() == pytest.approx(0.0)
    assert stage.GetEndTimeCode() == pytest.approx(5.0)
    assert pressure.GetTimeSamples() == pytest.approx([0.0, 5.0])
    assert list(pressure.Get(5.0)) == pytest.approx([110.0, 111.0, 112.0])


@pytest.mark.integration
def test_unrst_time_step_picks_sample_index():
    """Legacy `unrstTimeAxis=sampleIndex` is now
    `timeSource=TimeStep` with `timeScale=1.0`."""
    stage = _open_unrst_stage({"timeSource": "TimeStep", "timeScale": "1.0"})
    prim = stage.GetDefaultPrim()
    pressure = prim.GetAttribute("omni:sci:array:PRESSURE:value")

    assert stage.GetStartTimeCode() == pytest.approx(0.0)
    assert stage.GetEndTimeCode() == pytest.approx(1.0)
    assert pressure.GetTimeSamples() == pytest.approx([0.0, 1.0])


@pytest.mark.integration
def test_unrst_all_cell_sized_escape_hatch():
    stage = _open_unrst_stage({"reservoirKeywordMode": "allCellSized"})
    prim = stage.GetDefaultPrim()
    assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "CUSTOM")

    custom_api = OmniSciReservoir.CellPropertyAPI(prim, "CUSTOM")
    assert custom_api.GetIndexSpaceAttr().Get(Usd.TimeCode.EarliestTime()) == "logicalCells"
    assert custom_api.GetSourceKeywordAttr().Get(Usd.TimeCode.EarliestTime()) == "CUSTOM"

    custom = prim.GetAttribute("omni:sci:array:CUSTOM:value")
    assert custom.GetTimeSamples() == pytest.approx([0.0, _DEFAULT_END_TIMECODE])
    assert list(custom.Get(0.0)) == pytest.approx([1.0, 2.0, 3.0, 4.0])
    assert list(custom.Get(_DEFAULT_END_TIMECODE)) == pytest.approx([5.0, 6.0, 7.0, 8.0])
