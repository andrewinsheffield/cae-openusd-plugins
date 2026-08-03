# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""NanoVDB helper reader."""

import json
import os

import numpy as np
from pxr import OmniSci, Tf, Usd, Vt


def _extension(path: str) -> str:
    return path.rsplit(".", 1)[-1].lower() if "." in path else ""


def _dataset_name(path: str) -> str:
    return Tf.MakeValidIdentifier(os.path.splitext(os.path.basename(path))[0]) or "nvdb"


def _dataset_path(path: str) -> str:
    return f"/{_dataset_name(path)}"


def can_read(path: str) -> bool:
    return _extension(path) == "nvdb"


def read(layer, path: str, _metadata_only: bool, args: dict) -> list[dict]:
    prim_path = _dataset_path(path)
    lazy_fields = [
        {
            "primPath": prim_path,
            "attrName": "omni:sci:array:nanovdb:value",
            "typeName": "uint[]",
            "token": json.dumps({"array": "nanovdb"}),
        }
    ]

    stage = Usd.Stage.CreateInMemory()
    dataset = OmniSci.Dataset.Define(stage, prim_path)
    prim = dataset.GetPrim()
    stage.SetDefaultPrim(prim)
    OmniSci.ArrayAPI.Apply(prim, "nanovdb")
    OmniSci.ArrayAPI(prim, "nanovdb").CreateDeviceAttr("cpu")
    layer.TransferContent(stage.GetRootLayer())
    return lazy_fields


def _load_with_warp(path: str):
    try:
        import warp as wp
    except ImportError as exc:
        raise RuntimeError("omniSciNvdbFileFormat requires the Python 'warp' package from warp-lang.") from exc

    with open(path, "rb") as stream:
        volume = wp.Volume.load_from_nvdb(stream)
    return volume.array().numpy().view(np.uint32)


def load_array(path: str, token: str, _time, _args: dict) -> list:
    spec = json.loads(token)
    if spec["array"] != "nanovdb":
        raise RuntimeError(f"Unsupported NanoVDB lazy array '{spec['array']}'.")

    values = _load_with_warp(path)
    return Vt.UIntArray.FromNumpy(np.ascontiguousarray(values, dtype=np.uint32))
