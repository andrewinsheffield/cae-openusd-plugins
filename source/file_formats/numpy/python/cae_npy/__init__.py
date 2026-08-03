# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import json

import numpy as np
from pxr import Usd

from . import _schema_npy
from .utils import load_npy_array, numpy_to_vt_array


def _extension(path: str) -> str:
    return path.rsplit(".", 1)[-1].lower() if "." in path else ""


def can_read(path: str) -> bool:
    return _extension(path) == "npy"


def read(layer, path: str, _metadata_only: bool, args: dict) -> list[dict]:
    scratch_stage = Usd.Stage.CreateInMemory()
    lazy_fields = _schema_npy.read(scratch_stage, path, args)
    layer.TransferContent(scratch_stage.GetRootLayer())
    return lazy_fields


def load_array(path: str, token: str, time, args: dict) -> list:
    allow_pickle = str(args.get("allowPickle", "")).lower() == "true"
    spec = json.loads(token)
    array = load_npy_array(path, allow_pickle=allow_pickle)

    if "components" in spec:
        components = int(spec["components"])
        array = np.asarray(array).reshape((-1, components))
    if spec.get("flatten"):
        array = np.asarray(array).reshape(-1)
    return numpy_to_vt_array(array)
