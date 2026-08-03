# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Sample Python reader used by the PythonProxyFileFormat integration tests.

This module demonstrates the three callback functions that a Python-backed
USD file format reader must (or may optionally) implement when used with
``PythonFileFormatBase`` / ``PythonProxyFileFormat``:

``read(layer, path, metadata_only, args)``
    **Required.**  Author the USD prim/attribute structure into *layer* and
    return an optional lazy-field manifest so that large arrays are loaded
    on demand rather than up front.

``can_read(path)``
    **Optional.**  Return ``True`` when this reader can handle the file at
    *path*.  When absent, ``PythonFileFormatBase::CanRead`` falls back to
    a plain file-extension check.

``load_array(path, token, time, args)``
    **Required when lazy fields are declared.**  Return the array value
    identified by the opaque *token* (and *time* for time-sampled attributes).
    The return type must be compatible with the ``typeName`` declared in the
    lazy-field manifest (``float[]``, ``double[]``, ``int[]``, or ``int64[]``).

Lazy-field manifest format
--------------------------
The ``read`` callback returns a list of dicts (or a dict with key
``"lazyFields"``, or ``None`` / absent for no lazy fields):

.. code-block:: python

    [
        {
            "primPath": "/data",         # str -- SdfPath
            "attrName": "values",        # str -- attribute name token
            "typeName": "float[]",       # str -- one of the four supported types
            # Static (non-time-sampled) attribute:
            "token": "my_opaque_key",
            # OR time-sampled attribute (mutually exclusive with "token"):
            "timeSamples": [
                {"time": 0.0, "token": "key_at_0"},
                {"time": 1.0, "token": "key_at_1"},
            ],
        },
        ...
    ]
"""

from pxr import Sdf, Usd

LOAD_COUNTS = {}


def reset_load_counts():
    LOAD_COUNTS.clear()


def get_load_count(token):
    return LOAD_COUNTS.get(token, 0)


def read(layer, path, metadata_only, args):
    """Author the USD structure into *layer* and declare lazy array fields.

    Creates a ``/data`` Scope as the layer's default prim, with no ``/World``
    wrapper, and two float-array attributes:

    - ``values``: a static (non-time-sampled) array loaded via token
      ``"values"``.
    - ``series``: a time-sampled array with samples at t=0 and t=1,
      loaded via tokens ``"series:0"`` and ``"series:1"`` respectively.

    Parameters
    ----------
    layer:
        The ``Sdf.Layer`` to author into.  Use ``layer.TransferContent``
        or direct ``Sdf`` editing APIs.
    path:
        Resolved file path (the ``.pydf`` file path).
    metadata_only:
        When ``True``, only layer metadata needs to be populated.
        Large data loading can be skipped.
    args:
        ``dict[str, str]`` of the format arguments the layer was opened with
        (e.g. ``pythonModule``, ``pythonPath``).

    Returns
    -------
    list[dict]
        Lazy-field manifest; each dict describes one attribute whose value
        will be fetched on demand via :func:`load_array`.
    """
    scratch_stage = Usd.Stage.CreateInMemory()

    data = scratch_stage.DefinePrim("/data", "Scope")
    scratch_stage.SetDefaultPrim(data)
    data.CreateAttribute("values", Sdf.ValueTypeNames.FloatArray, True)
    data.CreateAttribute("series", Sdf.ValueTypeNames.FloatArray, True)

    layer.TransferContent(scratch_stage.GetRootLayer())
    return [
        {
            "primPath": "/data",
            "attrName": "values",
            "typeName": "float[]",
            "token": "values",
        },
        {
            "primPath": "/data",
            "attrName": "series",
            "typeName": "float[]",
            "timeSamples": [
                {"time": 0.0, "token": "series:0"},
                {"time": 1.0, "token": "series:1"},
            ],
        },
    ]


def can_read(path):
    """Return ``True`` when *path* has the ``.pydf`` extension.

    Parameters
    ----------
    path:
        Resolved file path to probe.

    Returns
    -------
    bool
    """
    return path.endswith(".pydf")


def load_array(path, token, time, args):
    """Return the array value identified by *token* (and *time* if animated).

    Parameters
    ----------
    path:
        Resolved file path (same value passed to :func:`read`).
    token:
        Opaque string key declared in the lazy-field manifest returned by
        :func:`read`.
    time:
        ``float`` time code for time-sampled attributes; ``None`` for
        static attributes.
    args:
        ``dict[str, str]`` of the format arguments the layer was opened with.

    Returns
    -------
    list[float]
        Array compatible with the ``typeName`` declared for this token.

    Raises
    ------
    ValueError
        When *token* is not recognised.
    """
    LOAD_COUNTS[token] = LOAD_COUNTS.get(token, 0) + 1

    if token == "values":
        return [1.0, 2.0, 3.5]
    if token == "series:0":
        return [0.0, 0.5]
    if token == "series:1":
        return [1.0, 1.5]
    raise ValueError(f"unknown token: {token}")
