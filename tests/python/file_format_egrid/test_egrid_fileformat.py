# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciReservoir as OmniSciReservoir


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "reservoir"
_CASES = [
    pytest.param("01_single_cell", "SINGLE_CELL", (1, 1, 1), 1, id="single_cell"),
    pytest.param("02_tiny", "TINY", (3, 3, 2), 18, id="tiny"),
    pytest.param("03_small_cubic", "SMALL_CUBIC", (10, 10, 10), 1000, id="small_cubic"),
    pytest.param("04_anisotropic", "ANISOTROPIC", (10, 8, 5), 400, id="anisotropic"),
    pytest.param("05_with_inactive", "WITH_INACTIVE", (10, 10, 5), 428, id="with_inactive"),
    pytest.param("06_with_properties", "WITH_PROPERTIES", (10, 10, 5), 500, id="with_properties"),
    pytest.param("07_thin_slice", "THIN_SLICE", (20, 20, 1), 400, id="thin_slice"),
    pytest.param("08_column", "COLUMN", (1, 1, 20), 20, id="column"),
    pytest.param("09_medium", "MEDIUM", (30, 30, 10), 9000, id="medium"),
]


def _egrid_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciEgridFileFormat") is not None


def _case_path(subdir, name):
    return _DATA_DIR / subdir / f"{name}.EGRID"


def _open_egrid_stage(path):
    if not path.exists():
        pytest.skip(f"EGRID fixture not found: {path}")
    if not _egrid_available():
        pytest.skip("OmniSciEgridFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(path))
    assert stage
    return stage


def _assert_logical_to_active_matches_actnum(prim, actnum, active_count):
    lookup = prim.GetAttribute("omni:sci:array:logicalCellToActiveCell:value").Get(Usd.TimeCode.EarliestTime())
    assert lookup is not None and len(lookup) == len(actnum)

    next_active = 0
    for active_flag, active_index in zip(actnum, lookup, strict=True):
        if active_flag != 0:
            assert active_index == next_active
            next_active += 1
        else:
            assert active_index == -1
    assert next_active == active_count


@pytest.mark.integration
def test_plugin_registered():
    assert _egrid_available(), "OmniSciEgridFileFormat not registered"


@pytest.mark.integration
@pytest.mark.parametrize("subdir,name,dims,active_count", _CASES)
def test_egrid_generated_fixtures_load(subdir, name, dims, active_count):
    stage = _open_egrid_stage(_case_path(subdir, name))
    prim = stage.GetDefaultPrim()
    assert prim
    assert prim.GetPath() == Sdf.Path(f"/{name}")
    assert prim.IsA(OmniSci.Dataset)
    assert prim.HasAPI(OmniSciReservoir.CornerPointGridAPI)
    assert "OmniSciCaeMeshAPI" not in prim.GetAppliedSchemas()

    api = OmniSciReservoir.CornerPointGridAPI(prim)
    assert api.GetLogicalCellDimsAttr().Get(Usd.TimeCode.EarliestTime()) == dims
    assert api.GetSourceFormatAttr().Get(Usd.TimeCode.EarliestTime()) == "egrid"
    assert api.GetIndexOrderAttr().Get(Usd.TimeCode.EarliestTime()) == "eclipse"
    assert api.GetDepthDirectionAttr().Get(Usd.TimeCode.EarliestTime()) == "zDown"
    assert api.GetLengthUnitAttr().Get(Usd.TimeCode.EarliestTime()) == "m"

    coord = prim.GetAttribute("omni:sci:array:coord:value").Get(Usd.TimeCode.EarliestTime())
    zcorn = prim.GetAttribute("omni:sci:array:zcorn:value").Get(Usd.TimeCode.EarliestTime())
    actnum = prim.GetAttribute("omni:sci:array:actnum:value").Get(Usd.TimeCode.EarliestTime())

    nx, ny, nz = dims
    logical_cell_count = nx * ny * nz
    assert coord is not None and len(coord) == 6 * (nx + 1) * (ny + 1)
    assert zcorn is not None and len(zcorn) == 8 * logical_cell_count
    assert actnum is not None and len(actnum) == logical_cell_count
    assert sum(1 for value in actnum if value != 0) == active_count
    _assert_logical_to_active_matches_actnum(prim, actnum, active_count)

    if name == "SINGLE_CELL":
        assert list(coord[:6]) == pytest.approx([0, 0, 0, 0, 0, 10])
        assert list(zcorn[:8]) == pytest.approx([0, 0, 0, 0, 10, 10, 10, 10])
        assert list(actnum) == [1]
        assert list(prim.GetAttribute("omni:sci:array:logicalCellToActiveCell:value").Get(Usd.TimeCode.EarliestTime())) == [0]


@pytest.mark.integration
def test_egrid_mount_path_argument():
    path = _case_path("01_single_cell", "SINGLE_CELL")
    if not path.exists():
        pytest.skip(f"EGRID fixture not found: {path}")
    if not _egrid_available():
        pytest.skip("OmniSciEgridFileFormat plugin not registered")

    identifier = Sdf.Layer.CreateIdentifier(str(path), {"mountPath": "/Reservoir/Grid"})
    stage = Usd.Stage.Open(identifier)
    assert stage
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Reservoir")
    prim = stage.GetPrimAtPath("/Reservoir/Grid")
    assert prim
    assert prim.HasAPI(OmniSciReservoir.CornerPointGridAPI)
    assert len(prim.GetAttribute("omni:sci:array:coord:value").Get(Usd.TimeCode.EarliestTime())) == 24
