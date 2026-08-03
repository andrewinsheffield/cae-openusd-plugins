# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Trimesh-backed surface mesh reader."""

import json
import os

import numpy as np
from pxr import OmniSci, OmniSciCae, Tf, Usd, Vt


_EXTENSIONS = {"stl", "ply", "3mf"}


def _extension(path: str) -> str:
    return path.rsplit(".", 1)[-1].lower() if "." in path else ""


def _dataset_name(path: str) -> str:
    return Tf.MakeValidIdentifier(os.path.splitext(os.path.basename(path))[0]) or "trimesh"


def _dataset_path(path: str) -> str:
    return f"/{_dataset_name(path)}"


def _register_array(prim, prim_path: str, instance: str, type_name: str, token: dict, lazy_fields: list) -> None:
    if prim is not None:
        OmniSci.ArrayAPI.Apply(prim, instance)
        OmniSci.ArrayAPI(prim, instance).CreateDeviceAttr("cpu")
    lazy_fields.append(
        {
            "primPath": prim_path,
            "attrName": f"omni:sci:array:{instance}:value",
            "typeName": type_name,
            "token": json.dumps(token),
        }
    )


def can_read(path: str) -> bool:
    return _extension(path) in _EXTENSIONS


def read(layer, path: str, _metadata_only: bool, args: dict) -> list[dict]:
    prim_path = _dataset_path(path)
    lazy_fields = []

    stage = Usd.Stage.CreateInMemory()
    dataset = OmniSci.Dataset.Define(stage, prim_path)
    prim = dataset.GetPrim()
    stage.SetDefaultPrim(prim)
    OmniSciCae.MeshAPI.Apply(prim)

    _register_array(prim, prim_path, "points", "float3[]", {"array": "points"}, lazy_fields)
    _register_array(
        prim,
        prim_path,
        "faceVertexIndices",
        "int[]",
        {"array": "faceVertexIndices"},
        lazy_fields,
    )
    _register_array(
        prim,
        prim_path,
        "faceVertexCounts",
        "int[]",
        {"array": "faceVertexCounts"},
        lazy_fields,
    )

    layer.TransferContent(stage.GetRootLayer())
    return lazy_fields


def _load_mesh(path: str):
    try:
        import trimesh
    except ImportError as exc:
        raise RuntimeError("Reading Trimesh arrays requires the 'trimesh' Python package.") from exc

    mesh = trimesh.load(path, force="mesh")
    if not isinstance(mesh, trimesh.Trimesh):
        raise RuntimeError(f"trimesh.load('{path}') returned {type(mesh).__name__}; expected a single Trimesh.")
    return mesh


def load_array(path: str, token: str, _time, _args: dict) -> list:
    spec = json.loads(token)
    mesh = _load_mesh(path)

    if spec["array"] == "points":
        return Vt.Vec3fArray.FromNumpy(np.ascontiguousarray(mesh.vertices, dtype=np.float32))
    if spec["array"] == "faceVertexIndices":
        return Vt.IntArray.FromNumpy(np.ascontiguousarray(mesh.faces, dtype=np.int32).reshape(-1))
    if spec["array"] == "faceVertexCounts":
        return Vt.IntArray.FromNumpy(np.full(len(mesh.faces), 3, dtype=np.int32))

    raise RuntimeError(f"Unsupported Trimesh lazy array '{spec['array']}'.")
