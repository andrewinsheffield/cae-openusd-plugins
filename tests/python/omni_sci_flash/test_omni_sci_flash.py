# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

from pxr import Tf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciFlash as OmniSciFlash


def test_plugin_registered():
    assert not Tf.Type.FindByName("OmniSciFlashAmrAPI").isUnknown


def test_amr_api_properties(stage):
    prim = OmniSci.Dataset.Define(stage, "/Flash").GetPrim()
    api = OmniSciFlash.AmrAPI.Apply(prim)
    assert api
    assert prim.HasAPI(OmniSciFlash.AmrAPI)

    api.CreateSpatialDimensionAttr().Set(2)
    api.CreateGidShapeAttr().Set([9])
    api.CreateBoundingBoxShapeAttr().Set([3, 2])
    api.CreateFieldShapeAttr().Set([1, 8, 8])

    assert api.GetSpatialDimensionAttr().Get() == 2
    assert list(api.GetGidShapeAttr().Get()) == [9]
    assert list(api.GetBoundingBoxShapeAttr().Get()) == [3, 2]
    assert list(api.GetFieldShapeAttr().Get()) == [1, 8, 8]
