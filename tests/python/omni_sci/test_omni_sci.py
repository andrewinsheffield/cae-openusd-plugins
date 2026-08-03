# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Tests for the OmniSci schema library (OmniSciDataset, FieldAPI, ArrayAPI)."""

import pytest
from pxr import OmniSci, Tf, Usd


# ---------------------------------------------------------------------------
# Plugin loading
# ---------------------------------------------------------------------------
def test_plugin_registered():
    assert not Tf.Type.FindByName("OmniSciDataset").isUnknown


# ---------------------------------------------------------------------------
# OmniSciDataset
# ---------------------------------------------------------------------------
class TestDataset:
    def test_define(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        assert ds
        assert ds.GetPrim().IsValid()

    def test_is_typed(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        assert ds.GetPrim().IsA(OmniSci.Dataset)

    def test_get(self, stage):
        OmniSci.Dataset.Define(stage, "/Sim")
        ds = OmniSci.Dataset.Get(stage, "/Sim")
        assert ds


# ---------------------------------------------------------------------------
# OmniSciFieldAPI
# ---------------------------------------------------------------------------
class TestFieldAPI:
    def test_apply(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        api = OmniSci.FieldAPI.Apply(ds.GetPrim(), "velocity")
        assert api
        assert ds.GetPrim().HasAPI(OmniSci.FieldAPI, "velocity")

    def test_association_default(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        api = OmniSci.FieldAPI.Apply(ds.GetPrim(), "p")
        assert api.GetAssociationAttr().Get() == "none"

    def test_association_node(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        api = OmniSci.FieldAPI.Apply(ds.GetPrim(), "velocity")
        api.CreateAssociationAttr().Set("node")
        assert api.GetAssociationAttr().Get() == "node"

    def test_association_element(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        api = OmniSci.FieldAPI.Apply(ds.GetPrim(), "pressure")
        api.CreateAssociationAttr().Set("element")
        assert api.GetAssociationAttr().Get() == "element"

    def test_name_attr(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        api = OmniSci.FieldAPI.Apply(ds.GetPrim(), "velocity")
        api.CreateNameAttr().Set("U")
        assert api.GetNameAttr().Get() == "U"

    def test_multiple_fields(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        OmniSci.FieldAPI.Apply(ds.GetPrim(), "velocity")
        OmniSci.FieldAPI.Apply(ds.GetPrim(), "pressure")
        assert ds.GetPrim().HasAPI(OmniSci.FieldAPI, "velocity")
        assert ds.GetPrim().HasAPI(OmniSci.FieldAPI, "pressure")

    def test_instance_names_are_independent(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        vel = OmniSci.FieldAPI.Apply(ds.GetPrim(), "velocity")
        prs = OmniSci.FieldAPI.Apply(ds.GetPrim(), "pressure")
        vel.CreateAssociationAttr().Set("node")
        prs.CreateAssociationAttr().Set("element")
        assert vel.GetAssociationAttr().Get() == "node"
        assert prs.GetAssociationAttr().Get() == "element"


# ---------------------------------------------------------------------------
# OmniSciArrayAPI
# ---------------------------------------------------------------------------
class TestArrayAPI:
    def test_apply(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        api = OmniSci.ArrayAPI.Apply(ds.GetPrim(), "velocity")
        assert api
        assert ds.GetPrim().HasAPI(OmniSci.ArrayAPI, "velocity")

    def test_device_unset_by_default(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        api = OmniSci.ArrayAPI.Apply(ds.GetPrim(), "velocity")
        # device has no schema default -- attribute exists but value is empty
        assert api.GetDeviceAttr()

    def test_device_cpu(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        api = OmniSci.ArrayAPI.Apply(ds.GetPrim(), "pressure")
        api.CreateDeviceAttr().Set("cpu")
        assert api.GetDeviceAttr().Get() == "cpu"

    def test_device_cuda(self, stage):
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        api = OmniSci.ArrayAPI.Apply(ds.GetPrim(), "velocity")
        api.CreateDeviceAttr().Set("cuda")
        assert api.GetDeviceAttr().Get() == "cuda"

    def test_field_and_array_paired(self, stage):
        """FieldAPI and ArrayAPI share instance name as the pairing key."""
        ds = OmniSci.Dataset.Define(stage, "/Sim")
        OmniSci.FieldAPI.Apply(ds.GetPrim(), "temperature")
        OmniSci.ArrayAPI.Apply(ds.GetPrim(), "temperature")
        assert ds.GetPrim().HasAPI(OmniSci.FieldAPI, "temperature")
        assert ds.GetPrim().HasAPI(OmniSci.ArrayAPI, "temperature")


# ---------------------------------------------------------------------------
# Round-trip: write a .usda, re-open, verify attributes survive
# ---------------------------------------------------------------------------
def test_roundtrip(tmp_path):
    path = str(tmp_path / "sim.usda")

    # Write
    s = Usd.Stage.CreateNew(path)
    ds = OmniSci.Dataset.Define(s, "/Sim")
    vel = OmniSci.FieldAPI.Apply(ds.GetPrim(), "velocity")
    vel.CreateNameAttr().Set("U")
    vel.CreateAssociationAttr().Set("node")
    arr = OmniSci.ArrayAPI.Apply(ds.GetPrim(), "velocity")
    arr.CreateDeviceAttr().Set("cuda")
    s.GetRootLayer().Save()

    # Re-open
    s2 = Usd.Stage.Open(path)
    ds2 = OmniSci.Dataset.Get(s2, "/Sim")
    assert ds2
    vel2 = OmniSci.FieldAPI(ds2.GetPrim(), "velocity")
    assert vel2.GetNameAttr().Get() == "U"
    assert vel2.GetAssociationAttr().Get() == "node"
    arr2 = OmniSci.ArrayAPI(ds2.GetPrim(), "velocity")
    assert arr2.GetDeviceAttr().Get() == "cuda"
