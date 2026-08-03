# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""NPY reader: single raw array, no geometry schema."""

import json

from pxr import OmniSci, Usd

from .utils import (
    dataset_name,
    dataset_path as make_dataset_path,
    make_dataset_prim,
    read_npy_metadata,
    register_array,
    shaped_array_type_name,
    unique_identifier,
)


def _array_name(args: dict) -> str:
    return args.get("arrayName") or "array"


def read(stage: Usd.Stage | None, path: str, args: dict) -> list[dict]:
    """Populate *stage* from a single-array NPY file.

    The file contributes one raw ``OmniSciArrayAPI`` instance on the dataset
    prim.  No point-cloud, CGNS, or field schema is inferred from the array
    contents.
    """
    requested_name = _array_name(args)
    info = read_npy_metadata(path, requested_name)
    type_name, components = shaped_array_type_name(info)
    if type_name is None:
        raise RuntimeError(f"Unsupported NPY dtype '{info.dtype}' in '{path}'")

    dataset_prim = None
    if stage is not None:
        dataset_prim, dataset_path = make_dataset_prim(stage, path, args)
    else:
        dataset_path = make_dataset_path(dataset_name(path, args), args)

    inst = unique_identifier(info.name, set())
    lazy_fields = []
    if dataset_prim is not None:
        OmniSci.ArrayAPI.Apply(dataset_prim, inst)
    token = {"array": info.name}
    if components is None:
        token["flatten"] = True
    else:
        token["components"] = components
    register_array(
        dataset_prim,
        dataset_path,
        inst,
        type_name,
        json.dumps(token),
        lazy_fields,
    )
    return lazy_fields
