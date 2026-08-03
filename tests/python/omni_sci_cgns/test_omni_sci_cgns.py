# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Tests for the OmniSciCgns schema library."""

import pytest
from pxr import OmniSciCgns, Sdf, Tf, Usd


# ---------------------------------------------------------------------------
# Plugin loading
# ---------------------------------------------------------------------------
def test_plugin_registered():
    assert not Tf.Type.FindByName("OmniSciCgnsZoneAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciCgnsGridCoordinatesAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciCgnsFlowSolutionAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciCgnsUnstructuredElementsAPI").isUnknown


# ---------------------------------------------------------------------------
# OmniSciCgnsZoneAPI
# ---------------------------------------------------------------------------
class TestZoneAPI:
    def test_apply(self, stage):
        prim = stage.DefinePrim("/Zone", "Xform")
        api = OmniSciCgns.ZoneAPI.Apply(prim)
        assert api
        assert prim.HasAPI(OmniSciCgns.ZoneAPI)

    def test_sections_rel(self, stage):
        zone = stage.DefinePrim("/Zone", "Xform")
        sec = stage.DefinePrim("/Zone/Sec1", "Xform")
        api = OmniSciCgns.ZoneAPI.Apply(zone)
        api.GetSectionsRel().AddTarget(sec.GetPath())
        targets = api.GetSectionsRel().GetTargets()
        assert Sdf.Path("/Zone/Sec1") in targets

    def test_flow_solutions_rel(self, stage):
        zone = stage.DefinePrim("/Zone", "Xform")
        sol = stage.DefinePrim("/Zone/FlowSol", "Xform")
        api = OmniSciCgns.ZoneAPI.Apply(zone)
        api.GetFlowSolutionsRel().AddTarget(sol.GetPath())
        targets = api.GetFlowSolutionsRel().GetTargets()
        assert Sdf.Path("/Zone/FlowSol") in targets

    def test_grid_coordinates_rel(self, stage):
        zone = stage.DefinePrim("/Zone", "Xform")
        coords = stage.DefinePrim("/Zone/GridCoordinates", "Xform")
        api = OmniSciCgns.ZoneAPI.Apply(zone)
        api.GetGridCoordinatesRel().AddTarget(coords.GetPath())
        targets = api.GetGridCoordinatesRel().GetTargets()
        assert Sdf.Path("/Zone/GridCoordinates") in targets

    def test_multiple_sections(self, stage):
        zone = stage.DefinePrim("/Zone", "Xform")
        s1 = stage.DefinePrim("/Zone/Sec1", "Xform")
        s2 = stage.DefinePrim("/Zone/Sec2", "Xform")
        api = OmniSciCgns.ZoneAPI.Apply(zone)
        api.GetSectionsRel().AddTarget(s1.GetPath())
        api.GetSectionsRel().AddTarget(s2.GetPath())
        assert len(api.GetSectionsRel().GetTargets()) == 2


# ---------------------------------------------------------------------------
# OmniSciCgnsGridCoordinatesAPI
# ---------------------------------------------------------------------------
class TestGridCoordinatesAPI:
    def test_apply(self, stage):
        prim = stage.DefinePrim("/GridCoordinates", "Xform")
        api = OmniSciCgns.GridCoordinatesAPI.Apply(prim)
        assert api
        assert prim.HasAPI(OmniSciCgns.GridCoordinatesAPI)


# ---------------------------------------------------------------------------
# OmniSciCgnsFlowSolutionAPI
# ---------------------------------------------------------------------------
class TestFlowSolutionAPI:
    def test_apply(self, stage):
        prim = stage.DefinePrim("/FlowSol", "Xform")
        api = OmniSciCgns.FlowSolutionAPI.Apply(prim)
        assert api
        assert prim.HasAPI(OmniSciCgns.FlowSolutionAPI)

    def test_grid_location_default(self, stage):
        prim = stage.DefinePrim("/FlowSol", "Xform")
        api = OmniSciCgns.FlowSolutionAPI.Apply(prim)
        assert api.GetGridLocationAttr().Get() == "Vertex"

    def test_grid_location_cell_center(self, stage):
        prim = stage.DefinePrim("/FlowSol", "Xform")
        api = OmniSciCgns.FlowSolutionAPI.Apply(prim)
        api.CreateGridLocationAttr().Set("CellCenter")
        assert api.GetGridLocationAttr().Get() == "CellCenter"

    @pytest.mark.parametrize("loc", [
        "Vertex", "CellCenter", "IFaceCenter", "JFaceCenter",
        "KFaceCenter", "FaceCenter", "EdgeCenter",
    ])
    def test_allowed_grid_locations(self, stage, loc):
        prim = stage.DefinePrim("/FlowSol", "Xform")
        api = OmniSciCgns.FlowSolutionAPI.Apply(prim)
        api.CreateGridLocationAttr().Set(loc)
        assert api.GetGridLocationAttr().Get() == loc


# ---------------------------------------------------------------------------
# OmniSciCgnsUnstructuredElementsAPI
# ---------------------------------------------------------------------------
class TestUnstructuredElementsAPI:
    def test_apply(self, stage):
        prim = stage.DefinePrim("/Sec", "Xform")
        api = OmniSciCgns.UnstructuredElementsAPI.Apply(prim)
        assert api
        assert prim.HasAPI(OmniSciCgns.UnstructuredElementsAPI)

    def test_element_type_default(self, stage):
        prim = stage.DefinePrim("/Sec", "Xform")
        api = OmniSciCgns.UnstructuredElementsAPI.Apply(prim)
        assert api.GetElementTypeAttr().Get() == "ElementTypeNull"

    @pytest.mark.parametrize("etype", ["HEXA_8", "TETRA_4", "MIXED", "NGON_n"])
    def test_element_types(self, stage, etype):
        prim = stage.DefinePrim("/Sec", "Xform")
        api = OmniSciCgns.UnstructuredElementsAPI.Apply(prim)
        api.CreateElementTypeAttr().Set(etype)
        assert api.GetElementTypeAttr().Get() == etype

    def test_element_size_boundary_default(self, stage):
        prim = stage.DefinePrim("/Sec", "Xform")
        api = OmniSciCgns.UnstructuredElementsAPI.Apply(prim)
        assert api.GetElementSizeBoundaryAttr().Get() == 0

    def test_element_range(self, stage):
        from pxr import Gf
        prim = stage.DefinePrim("/Sec", "Xform")
        api = OmniSciCgns.UnstructuredElementsAPI.Apply(prim)
        api.CreateElementRangeAttr().Set(Gf.Vec2i(1, 100))
        val = api.GetElementRangeAttr().Get()
        assert val[0] == 1
        assert val[1] == 100

    def test_zone_rel(self, stage):
        zone = stage.DefinePrim("/Zone", "Xform")
        sec = stage.DefinePrim("/Zone/Sec", "Xform")
        api = OmniSciCgns.UnstructuredElementsAPI.Apply(sec)
        api.GetZoneRel().AddTarget(zone.GetPath())
        assert Sdf.Path("/Zone") in api.GetZoneRel().GetTargets()


# ---------------------------------------------------------------------------
# Round-trip
# ---------------------------------------------------------------------------
def test_roundtrip(tmp_path):
    path = str(tmp_path / "zone.usda")

    # Write
    s = Usd.Stage.CreateNew(path)
    zone = s.DefinePrim("/Zone", "Xform")
    coords = s.DefinePrim("/Zone/GridCoordinates", "Xform")
    sol = s.DefinePrim("/Zone/FlowSol", "Xform")
    sec = s.DefinePrim("/Zone/Sec1", "Xform")

    zone_api = OmniSciCgns.ZoneAPI.Apply(zone)
    zone_api.GetGridCoordinatesRel().AddTarget(coords.GetPath())
    zone_api.GetFlowSolutionsRel().AddTarget(sol.GetPath())
    zone_api.GetSectionsRel().AddTarget(sec.GetPath())

    OmniSciCgns.GridCoordinatesAPI.Apply(coords)

    sol_api = OmniSciCgns.FlowSolutionAPI.Apply(sol)
    sol_api.CreateGridLocationAttr().Set("CellCenter")

    sec_api = OmniSciCgns.UnstructuredElementsAPI.Apply(sec)
    sec_api.CreateElementTypeAttr().Set("HEXA_8")

    s.GetRootLayer().Save()

    # Re-open
    s2 = Usd.Stage.Open(path)
    zone2 = OmniSciCgns.ZoneAPI(s2.GetPrimAtPath("/Zone"))
    assert Sdf.Path("/Zone/GridCoordinates") in zone2.GetGridCoordinatesRel().GetTargets()
    assert Sdf.Path("/Zone/FlowSol") in zone2.GetFlowSolutionsRel().GetTargets()

    coords2 = OmniSciCgns.GridCoordinatesAPI(s2.GetPrimAtPath("/Zone/GridCoordinates"))
    assert coords2

    sol2 = OmniSciCgns.FlowSolutionAPI(s2.GetPrimAtPath("/Zone/FlowSol"))
    assert sol2.GetGridLocationAttr().Get() == "CellCenter"

    sec2 = OmniSciCgns.UnstructuredElementsAPI(s2.GetPrimAtPath("/Zone/Sec1"))
    assert sec2.GetElementTypeAttr().Get() == "HEXA_8"
