# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Tests for shared file-format argument API schemas."""

from pxr import OmniSciFileFormatArgs, Sdf, Tf


def _assert_uniform(*attrs):
    for attr in attrs:
        assert attr.GetVariability() == Sdf.VariabilityUniform


def test_plugin_registered():
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsArgsAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsTimeAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsStreamingAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsCgnsAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsEdemAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsVtkHdfAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsFlashAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsEnSightAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsNpzAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsNpyAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsOpenFoamAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsGrdeclAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsEgridAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsReservoirResultsAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsInitAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsUnrstAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsVtkAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciFileFormatArgsPythonAPI").isUnknown


def test_format_specific_apis_include_default_argument_apis(stage):
    args_time_streaming = [
        "OmniSciFileFormatArgsAPI",
        "OmniSciFileFormatArgsTimeAPI",
        "OmniSciFileFormatArgsStreamingAPI",
    ]
    args_streaming = [
        "OmniSciFileFormatArgsAPI",
        "OmniSciFileFormatArgsStreamingAPI",
    ]

    cases = [
        (
            OmniSciFileFormatArgs.CgnsAPI,
            [
                "OmniSciFileFormatArgsCgnsAPI",
                "OmniSciFileFormatArgsAPI",
                "OmniSciFileFormatArgsTimeAPI",
            ],
            [OmniSciFileFormatArgs.ArgsAPI, OmniSciFileFormatArgs.TimeAPI],
        ),
        (
            OmniSciFileFormatArgs.EdemAPI,
            ["OmniSciFileFormatArgsEdemAPI", *args_time_streaming],
            [
                OmniSciFileFormatArgs.ArgsAPI,
                OmniSciFileFormatArgs.TimeAPI,
                OmniSciFileFormatArgs.StreamingAPI,
            ],
        ),
        (
            OmniSciFileFormatArgs.FlashAPI,
            [
                "OmniSciFileFormatArgsFlashAPI",
                "OmniSciFileFormatArgsAPI",
                "OmniSciFileFormatArgsTimeAPI",
            ],
            [OmniSciFileFormatArgs.ArgsAPI, OmniSciFileFormatArgs.TimeAPI],
        ),
        (
            OmniSciFileFormatArgs.EnSightAPI,
            ["OmniSciFileFormatArgsEnSightAPI", *args_time_streaming],
            [
                OmniSciFileFormatArgs.ArgsAPI,
                OmniSciFileFormatArgs.TimeAPI,
                OmniSciFileFormatArgs.StreamingAPI,
            ],
        ),
        (
            OmniSciFileFormatArgs.NpzAPI,
            ["OmniSciFileFormatArgsNpzAPI", "OmniSciFileFormatArgsAPI"],
            [OmniSciFileFormatArgs.ArgsAPI],
        ),
        (
            OmniSciFileFormatArgs.NpyAPI,
            ["OmniSciFileFormatArgsNpyAPI", "OmniSciFileFormatArgsAPI"],
            [OmniSciFileFormatArgs.ArgsAPI],
        ),
        (
            OmniSciFileFormatArgs.OpenFoamAPI,
            ["OmniSciFileFormatArgsOpenFoamAPI", *args_time_streaming],
            [
                OmniSciFileFormatArgs.ArgsAPI,
                OmniSciFileFormatArgs.TimeAPI,
                OmniSciFileFormatArgs.StreamingAPI,
            ],
        ),
        (
            OmniSciFileFormatArgs.GrdeclAPI,
            ["OmniSciFileFormatArgsGrdeclAPI", "OmniSciFileFormatArgsAPI"],
            [OmniSciFileFormatArgs.ArgsAPI],
        ),
        (
            OmniSciFileFormatArgs.EgridAPI,
            ["OmniSciFileFormatArgsEgridAPI", "OmniSciFileFormatArgsAPI"],
            [OmniSciFileFormatArgs.ArgsAPI],
        ),
        (
            OmniSciFileFormatArgs.InitAPI,
            [
                "OmniSciFileFormatArgsInitAPI",
                "OmniSciFileFormatArgsReservoirResultsAPI",
                "OmniSciFileFormatArgsAPI",
            ],
            [OmniSciFileFormatArgs.ReservoirResultsAPI, OmniSciFileFormatArgs.ArgsAPI],
        ),
        (
            OmniSciFileFormatArgs.UnrstAPI,
            [
                "OmniSciFileFormatArgsUnrstAPI",
                "OmniSciFileFormatArgsReservoirResultsAPI",
                "OmniSciFileFormatArgsAPI",
                "OmniSciFileFormatArgsTimeAPI",
            ],
            [
                OmniSciFileFormatArgs.ReservoirResultsAPI,
                OmniSciFileFormatArgs.ArgsAPI,
                OmniSciFileFormatArgs.TimeAPI,
            ],
        ),
        (
            OmniSciFileFormatArgs.PythonAPI,
            ["OmniSciFileFormatArgsPythonAPI", "OmniSciFileFormatArgsAPI"],
            [OmniSciFileFormatArgs.ArgsAPI],
        ),
        (
            OmniSciFileFormatArgs.VtkAPI,
            ["OmniSciFileFormatArgsVtkAPI", *args_streaming],
            [OmniSciFileFormatArgs.ArgsAPI, OmniSciFileFormatArgs.StreamingAPI],
        ),
    ]

    for index, (api_type, expected_schemas, builtin_api_types) in enumerate(cases):
        prim = stage.DefinePrim(f"/Payload{index}", "Xform")
        api_type.Apply(prim)

        assert prim.GetAppliedSchemas() == expected_schemas
        for builtin_api_type in builtin_api_types:
            assert prim.HasAPI(builtin_api_type)


def test_common_args_api(stage):
    """ArgsAPI carries the cross-format lazy-array cache policy."""
    prim = stage.DefinePrim("/Payload", "Xform")
    api = OmniSciFileFormatArgs.ArgsAPI.Apply(prim)
    api.CreateCacheModeAttr().Set("static")

    assert prim.HasAPI(OmniSciFileFormatArgs.ArgsAPI)
    assert api.GetCacheModeAttr().GetName() == "omni:cae:format:cacheMode"
    _assert_uniform(api.GetCacheModeAttr())
    assert api.GetCacheModeAttr().Get() == "static"
    assert not prim.GetAttribute("omni:cae:format:rootName").IsValid()
    assert not prim.GetAttribute("omni:cae:format:addWorldPrim").IsValid()


def test_time_args_api(stage):
    prim = stage.DefinePrim("/Payload", "Xform")
    api = OmniSciFileFormatArgs.TimeAPI.Apply(prim)
    api.CreateScaleAttr().Set(0.001)
    api.CreateOffsetAttr().Set(2.0)
    api.CreateSourceAttr().Set("TimeValue")

    assert prim.HasAPI(OmniSciFileFormatArgs.TimeAPI)
    assert api.GetScaleAttr().GetName() == "omni:cae:format:time:scale"
    assert api.GetOffsetAttr().GetName() == "omni:cae:format:time:offset"
    assert api.GetSourceAttr().GetName() == "omni:cae:format:time:source"
    _assert_uniform(api.GetScaleAttr(), api.GetOffsetAttr(), api.GetSourceAttr())
    assert api.GetScaleAttr().Get() == 0.001
    assert api.GetOffsetAttr().Get() == 2.0
    assert api.GetSourceAttr().Get() == "TimeValue"


def test_flash_args_use_shared_time_defaults(stage):
    prim = stage.DefinePrim("/FlashPayload", "Xform")
    OmniSciFileFormatArgs.FlashAPI.Apply(prim)

    assert prim.HasAPI(OmniSciFileFormatArgs.FlashAPI)
    assert prim.HasAPI(OmniSciFileFormatArgs.TimeAPI)
    assert OmniSciFileFormatArgs.TimeAPI(prim).GetSourceAttr().Get() == "TimeStep"


def test_streaming_args_api(stage):
    prim = stage.DefinePrim("/Payload", "Xform")
    api = OmniSciFileFormatArgs.StreamingAPI.Apply(prim)
    api.CreateIoThreadsAttr().Set(4)

    assert prim.HasAPI(OmniSciFileFormatArgs.StreamingAPI)
    _assert_uniform(api.GetIoThreadsAttr())
    assert api.GetIoThreadsAttr().Get() == 4
    assert not prim.GetAttribute("omni:cae:format:streaming:chunkSize").IsValid()
    assert not prim.GetAttribute("omni:cae:format:streaming:prefetch").IsValid()
    assert not prim.GetAttribute("omni:cae:format:streaming:maxCachedChunks").IsValid()


def test_reservoir_results_args_api(stage):
    prim = stage.DefinePrim("/Payload", "Xform")
    api = OmniSciFileFormatArgs.ReservoirResultsAPI.Apply(prim)
    api.CreateReservoirKeywordModeAttr().Set("allCellSized")

    assert prim.HasAPI(OmniSciFileFormatArgs.ReservoirResultsAPI)
    assert api.GetReservoirKeywordModeAttr().GetName() == "omni:cae:format:reservoir:keywordMode"
    _assert_uniform(api.GetReservoirKeywordModeAttr())
    assert api.GetReservoirKeywordModeAttr().Get() == "allCellSized"


def test_unrst_args_api(stage):
    """UnrstAPI conforms to the generic TimeAPI surface rather than defining
    a per-format time-axis attribute."""
    prim = stage.DefinePrim("/Payload", "Xform")
    OmniSciFileFormatArgs.UnrstAPI.Apply(prim)

    assert prim.HasAPI(OmniSciFileFormatArgs.UnrstAPI)
    assert prim.HasAPI(OmniSciFileFormatArgs.ReservoirResultsAPI)
    assert prim.HasAPI(OmniSciFileFormatArgs.ArgsAPI)
    assert prim.HasAPI(OmniSciFileFormatArgs.TimeAPI)
    assert not prim.GetAttribute("omni:cae:format:unrst:timeAxis").IsValid()

    # The chained TimeAPI is real and authorable on the same prim.
    time_api = OmniSciFileFormatArgs.TimeAPI(prim)
    time_api.CreateSourceAttr().Set("IterationValue")
    time_api.CreateScaleAttr().Set(86400.0)
    assert time_api.GetSourceAttr().Get() == "IterationValue"
    assert time_api.GetScaleAttr().Get() == 86400.0


def test_cgns_args_api(stage):
    prim = stage.DefinePrim("/Payload", "Xform")
    api = OmniSciFileFormatArgs.CgnsAPI.Apply(prim)
    api.CreateBaseNameAttr().Set("Base")
    api.CreateZoneNameAttr().Set("StaticMixer")
    api.CreateIntSizeAttr().Set(64)
    api.CreateFloatSizeAttr().Set(32)

    assert prim.HasAPI(OmniSciFileFormatArgs.CgnsAPI)
    _assert_uniform(
        api.GetBaseNameAttr(),
        api.GetZoneNameAttr(),
        api.GetIntSizeAttr(),
        api.GetFloatSizeAttr(),
    )
    assert api.GetBaseNameAttr().Get() == "Base"
    assert api.GetZoneNameAttr().Get() == "StaticMixer"
    assert api.GetIntSizeAttr().Get() == 64
    assert api.GetFloatSizeAttr().Get() == 32


def test_npz_args_api(stage):
    prim = stage.DefinePrim("/Payload", "Xform")
    api = OmniSciFileFormatArgs.NpzAPI.Apply(prim)
    api.CreateSchemaAttr().Set("Point Cloud")
    api.CreateCoordsArrayAttr().Set("xyz")
    api.CreateCoordsArrayXAttr().Set("x")
    api.CreateCoordsArrayYAttr().Set("y")
    api.CreateCoordsArrayZAttr().Set("z")
    api.CreateAllowPickleAttr().Set(True)

    assert prim.HasAPI(OmniSciFileFormatArgs.NpzAPI)
    _assert_uniform(
        api.GetSchemaAttr(),
        api.GetCoordsArrayAttr(),
        api.GetCoordsArrayXAttr(),
        api.GetCoordsArrayYAttr(),
        api.GetCoordsArrayZAttr(),
        api.GetAllowPickleAttr(),
    )
    assert api.GetSchemaAttr().Get() == "Point Cloud"
    assert api.GetCoordsArrayAttr().Get() == "xyz"
    assert api.GetCoordsArrayXAttr().Get() == "x"
    assert api.GetCoordsArrayYAttr().Get() == "y"
    assert api.GetCoordsArrayZAttr().Get() == "z"
    assert api.GetAllowPickleAttr().Get() is True


def test_npy_args_api(stage):
    prim = stage.DefinePrim("/Payload", "Xform")
    api = OmniSciFileFormatArgs.NpyAPI.Apply(prim)
    api.CreateArrayNameAttr().Set("temperature")
    api.CreateAllowPickleAttr().Set(True)

    assert prim.HasAPI(OmniSciFileFormatArgs.NpyAPI)
    _assert_uniform(
        api.GetArrayNameAttr(),
        api.GetAllowPickleAttr(),
    )
    assert api.GetArrayNameAttr().Get() == "temperature"
    assert api.GetAllowPickleAttr().Get() is True


def test_python_args_api(stage):
    prim = stage.DefinePrim("/Payload", "Xform")
    api = OmniSciFileFormatArgs.PythonAPI.Apply(prim)
    api.CreateModuleAttr().Set("reader")
    api.CreatePathAttr().Set("/tmp/readers")
    api.CreateReadFunctionAttr().Set("read_layer")
    api.CreateCanReadFunctionAttr().Set("can_read_layer")
    api.CreateLoadArrayFunctionAttr().Set("load_field")

    assert prim.HasAPI(OmniSciFileFormatArgs.PythonAPI)
    _assert_uniform(
        api.GetModuleAttr(),
        api.GetPathAttr(),
        api.GetReadFunctionAttr(),
        api.GetCanReadFunctionAttr(),
        api.GetLoadArrayFunctionAttr(),
    )
    assert api.GetModuleAttr().Get() == "reader"
    assert api.GetPathAttr().Get() == "/tmp/readers"
    assert api.GetReadFunctionAttr().Get() == "read_layer"
    assert api.GetCanReadFunctionAttr().Get() == "can_read_layer"
    assert api.GetLoadArrayFunctionAttr().Get() == "load_field"
