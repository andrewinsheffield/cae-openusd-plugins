# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import json

import numpy as np
from pxr import Usd

from . import _schema_cgns, _schema_none, _schema_point_cloud
from .utils import numpy_to_vt_array


def _extension(path: str) -> str:
    return path.rsplit(".", 1)[-1].lower() if "." in path else ""


def can_read(path: str) -> bool:
    return _extension(path) == "npz"


def read(layer, path: str, _metadata_only: bool, args: dict) -> list[dict]:
    schema_raw = (args.get("schema") or "Point Cloud").strip()
    schema_lower = schema_raw.lower()
    scratch_stage = Usd.Stage.CreateInMemory()

    if schema_lower in {"point cloud", "pointcloud", "omnicaepointcloudapi"}:
        lazy_fields = _schema_point_cloud.read(scratch_stage, path, args)
    elif schema_lower in {"cgns", "omnicgns"}:
        lazy_fields = _schema_cgns.read(scratch_stage, path, args)
    elif schema_lower in {"none", "null"}:
        lazy_fields = _schema_none.read(scratch_stage, path, args)
    else:
        raise RuntimeError(
            f"Unsupported NPZ schema '{schema_raw}'. "
            "Supported schemas: 'Point Cloud', 'CGNS', 'None'."
        )

    layer.TransferContent(scratch_stage.GetRootLayer())
    return lazy_fields


def load_array(path: str, token: str, time, args: dict) -> list:
    allow_pickle = str(args.get("allowPickle", "")).lower() == "true"
    spec = json.loads(token)
    with np.load(path, allow_pickle=allow_pickle) as np_file:
        if not hasattr(np_file, "files"):
            raise RuntimeError(f"Unsupported NPZ payload in '{path}'")
        array = np_file[spec["array"]]
    if "column" in spec:
        array = array[:, int(spec["column"])]

    if spec.get("preferFloat"):
        array = np.asarray(array, dtype=np.float64)
    if "components" in spec:
        components = int(spec["components"])
        array = np.asarray(array).reshape((-1, components))
    if spec.get("flatten"):
        array = np.asarray(array).reshape(-1)
    return numpy_to_vt_array(array)
