// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciEdemFileFormat.h"

#include "CaeFileFormatData.h"
#include "DisablePXRWarnings.h"
#include "DynamicFileFormatArguments.h"
#include "FileFormatError.h"
#include "MountPath.h"
#include "ResolverAsset.h"
#include "UninitializedVtArray.h"
#include "debugCodes.h"

#include <omniSci/arrayAPI.h>
#include <omniSci/dataset.h>
#include <omniSci/fieldAPI.h>
#include <omniSci/tokens.h>
#include <omniSciCae/pointCloudAPI.h>
#include <omniSciCae/tokens.h>
#include <omniSciEdem/geometryGroupAPI.h>
#include <omniSciEdem/particleCloudAPI.h>
#include <omniSciEdem/particleTypeAPI.h>
#include <omniSciFileFormatArgs/tokens.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformOp.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <hdf5.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace fs = std::filesystem;

namespace detail
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 5> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciEdemFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeScale, OmniSciEdemFileFormatTokens->ArgTimeScale },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeOffset, OmniSciEdemFileFormatTokens->ArgTimeOffset },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeSource, OmniSciEdemFileFormatTokens->ArgTimeSource },
            { OmniSciFileFormatArgsTokens->omniCaeFormatStreamingIoThreads, OmniSciEdemFileFormatTokens->ArgIoThreads },
        } };

    return DynamicFileFormatArgs;
}

struct ReadOptions
{
    SdfPath rootPath;
    double timeScale = 1.0;
    double timeOffset = 0.0;
    std::string timeSource = "TimeStep";
    CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All;
    int ioThreads = 1;
};

struct TimeSampleInfo
{
    std::string filePath;
    std::string timestepPath;
    double caseTime = 0.0;
};

enum class ReadMode
{
    StructureOnly,
    FileDataOnly,
    StructureAndFileData,
};

enum class PrototypeKind
{
    Unknown,
    Polyhedral,
    SphereCluster
};

struct ParticleTypeInfo
{
    std::string sourceNode;
    std::string name;
    std::string creatorPath;
    PrototypeKind kind = PrototypeKind::Unknown;
};

struct GeometryGroupInfo
{
    std::string sourceNode;
    std::string name;
    std::string creatorPath;
};

struct SpherePrimitive
{
    std::string name;
    GfVec3f position = GfVec3f(0.0f);
    float physicalRadius = 0.0f;
    float contactRadius = 0.0f;
};

struct FieldInfo
{
    std::string sourceName;
    TfToken instanceName;
    TfToken valueType;
};

struct ParticleCloudInfo
{
    ParticleTypeInfo particleType;
    std::vector<FieldInfo> fields;
};

struct CaseInfo
{
    std::string casePath;
    std::string caseDir;
    std::string dataDir;
    ReadOptions options;
    std::vector<TimeSampleInfo> samples;
    std::vector<double> sampleTimes;
    bool sampleTimesAreSorted = true;
    std::vector<ParticleTypeInfo> particleTypes;
    std::vector<GeometryGroupInfo> geometryGroups;
    std::vector<ParticleCloudInfo> particleClouds;
};

struct ReadContext
{
    ReadContext() = default;
    ReadContext(const ReadContext&) = delete;
    ReadContext& operator=(const ReadContext&) = delete;
    ReadContext(ReadContext&&) noexcept = default;
    ReadContext& operator=(ReadContext&&) noexcept = default;

    CaseInfo caseInfo;
    ReadMode mode = ReadMode::StructureAndFileData;
    SdfLayerRefPtr layer;
    UsdStageRefPtr stage;
    CaeFileFormatDataRefPtr fileData;
};

using ReadEdemResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;

static TfToken MakeArrayValueAttrName(const TfToken& arrayName)
{
    return TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

class H5Handle
{
public:
    H5Handle() = default;
    explicit H5Handle(hid_t id) : _id(id)
    {
    }

    H5Handle(const H5Handle&) = delete;
    H5Handle& operator=(const H5Handle&) = delete;

    H5Handle(H5Handle&& other) noexcept : _id(other._id)
    {
        other._id = -1;
    }

    H5Handle& operator=(H5Handle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            _id = other._id;
            other._id = -1;
        }
        return *this;
    }

    ~H5Handle()
    {
        Reset();
    }

    hid_t Get() const
    {
        return _id;
    }

    explicit operator bool() const
    {
        return _id >= 0;
    }

    void Reset()
    {
        if (_id < 0)
            return;

        const H5I_type_t type = H5Iget_type(_id);
        switch (type)
        {
        case H5I_FILE:
            H5Fclose(_id);
            break;
        case H5I_GROUP:
            H5Gclose(_id);
            break;
        case H5I_DATASET:
            H5Dclose(_id);
            break;
        case H5I_DATASPACE:
            H5Sclose(_id);
            break;
        case H5I_DATATYPE:
            H5Tclose(_id);
            break;
        case H5I_ATTR:
            H5Aclose(_id);
            break;
        default:
            break;
        }
        _id = -1;
    }

private:
    hid_t _id = -1;
};

struct DatasetInfo
{
    H5T_class_t typeClass = H5T_NO_CLASS;
    std::vector<hsize_t> dims;
};

static std::string TrimNulls(std::string value)
{
    while (!value.empty() && value.back() == '\0')
        value.pop_back();
    return value;
}

static H5Handle OpenFileReadOnly(const std::string& path)
{
    hid_t id = -1;
    H5E_BEGIN_TRY
    {
        id = H5Fopen(path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    }
    H5E_END_TRY;
    H5Handle file(id);
    if (!file)
        throw cae::FileFormatError("Failed to open EDEM HDF5 file: " + path);
    return file;
}

static bool PathExists(hid_t loc, const std::string& path)
{
    htri_t exists = 0;
    H5E_BEGIN_TRY
    {
        exists = H5Lexists(loc, path.c_str(), H5P_DEFAULT);
    }
    H5E_END_TRY;
    return exists > 0;
}

static std::vector<std::string> ListChildNames(hid_t loc, const std::string& path)
{
    std::vector<std::string> out;
    hid_t groupId = -1;
    H5E_BEGIN_TRY
    {
        groupId = H5Gopen2(loc, path.c_str(), H5P_DEFAULT);
    }
    H5E_END_TRY;
    H5Handle group(groupId);
    if (!group)
        return out;

    H5G_info_t info{};
    if (H5Gget_info(group.Get(), &info) < 0)
        return out;

    out.reserve(info.nlinks);
    for (hsize_t i = 0; i < info.nlinks; ++i)
    {
        const auto nameSize =
            H5Lget_name_by_idx(group.Get(), ".", H5_INDEX_NAME, H5_ITER_INC, i, nullptr, 0, H5P_DEFAULT);
        if (nameSize < 0)
            continue;
        std::string name(static_cast<size_t>(nameSize) + 1u, '\0');
        H5Lget_name_by_idx(group.Get(), ".", H5_INDEX_NAME, H5_ITER_INC, i, name.data(), name.size(), H5P_DEFAULT);
        out.push_back(TrimNulls(name));
    }
    return out;
}

static std::string ReadStringAttr(hid_t loc, const std::string& attrName, const std::string& fallback = {})
{
    H5Handle attr(H5Aopen(loc, attrName.c_str(), H5P_DEFAULT));
    if (!attr)
        return fallback;

    H5Handle type(H5Aget_type(attr.Get()));
    if (!type || H5Tget_class(type.Get()) != H5T_STRING)
        return fallback;

    if (H5Tis_variable_str(type.Get()) > 0)
    {
        char* text = nullptr;
        if (H5Aread(attr.Get(), type.Get(), &text) < 0)
            return fallback;
        std::string result = text ? text : "";
        if (text)
            H5free_memory(text);
        return result;
    }

    const size_t size = H5Tget_size(type.Get());
    std::string result(size + 1u, '\0');
    if (H5Aread(attr.Get(), type.Get(), result.data()) < 0)
        return fallback;
    return TrimNulls(result);
}

template <typename T>
static T ReadScalarAttr(hid_t loc, const std::string& attrName, T fallback)
{
    H5Handle attr(H5Aopen(loc, attrName.c_str(), H5P_DEFAULT));
    if (!attr)
        return fallback;

    T value = fallback;
    hid_t memType = std::is_integral_v<T> ? H5T_NATIVE_LLONG : H5T_NATIVE_DOUBLE;
    if (H5Aread(attr.Get(), memType, &value) < 0)
        return fallback;
    return value;
}

static DatasetInfo InspectDataset(hid_t loc, const std::string& path)
{
    hid_t datasetId = -1;
    H5E_BEGIN_TRY
    {
        datasetId = H5Dopen2(loc, path.c_str(), H5P_DEFAULT);
    }
    H5E_END_TRY;
    H5Handle dataset(datasetId);
    if (!dataset)
        throw cae::FileFormatError("Failed to open HDF5 dataset: " + path);

    H5Handle datatype(H5Dget_type(dataset.Get()));
    H5Handle dataspace(H5Dget_space(dataset.Get()));
    if (!datatype || !dataspace)
        throw cae::FileFormatError("Failed to inspect HDF5 dataset: " + path);

    const int rank = H5Sget_simple_extent_ndims(dataspace.Get());
    std::vector<hsize_t> dims(static_cast<size_t>(std::max(rank, 0)));
    if (rank > 0)
        H5Sget_simple_extent_dims(dataspace.Get(), dims.data(), nullptr);

    return { H5Tget_class(datatype.Get()), std::move(dims) };
}

static H5O_type_t GetObjectType(hid_t loc, const std::string& path)
{
#if H5_VERSION_GE(1, 12, 0)
    H5O_info2_t info{};
#else
    H5O_info_t info{};
#endif
    herr_t status = -1;
    H5E_BEGIN_TRY
    {
#if H5_VERSION_GE(1, 12, 0)
        status = H5Oget_info_by_name(loc, path.c_str(), &info, H5O_INFO_BASIC, H5P_DEFAULT);
#else
        status = H5Oget_info_by_name(loc, path.c_str(), &info, H5P_DEFAULT);
#endif
    }
    H5E_END_TRY;
    if (status < 0)
        return H5O_TYPE_UNKNOWN;
    return info.type;
}

static bool IsDataset(hid_t loc, const std::string& path)
{
    return GetObjectType(loc, path) == H5O_TYPE_DATASET;
}

static size_t GetElementCount(const std::vector<hsize_t>& dims)
{
    size_t count = 1;
    for (hsize_t dim : dims)
        count *= dim;
    return count;
}

static void ReadNumericDatasetInto(hid_t loc, const std::string& path, hid_t memType, std::byte* values, size_t count)
{
    H5Handle dataset(H5Dopen2(loc, path.c_str(), H5P_DEFAULT));
    if (!dataset)
        throw cae::FileFormatError("Failed to open HDF5 dataset: " + path);

    if (count == 0)
        return;

    if (H5Dread(dataset.Get(), memType, H5S_ALL, H5S_ALL, H5P_DEFAULT, values) < 0)
        throw cae::FileFormatError("Failed to read HDF5 dataset: " + path);
}

template <typename T>
static std::unique_ptr<T[]> ReadNumericDatasetBuffer(hid_t loc, const std::string& path, hid_t memType, size_t count)
{
    // Keep this raw array: H5Dread overwrites every element, so value-initializing
    // a vector or make_unique<T[]> would add a full, unnecessary memory pass.
    std::unique_ptr<T[]> values = count > 0 ? std::unique_ptr<T[]>(new T[count]) : nullptr; // NOSONAR
    ReadNumericDatasetInto(loc, path, memType, static_cast<std::byte*>(static_cast<void*>(values.get())), count);
    return values;
}

template <typename T>
static std::vector<T> ReadNumericDataset(hid_t loc, const std::string& path, hid_t memType)
{
    H5Handle dataset(H5Dopen2(loc, path.c_str(), H5P_DEFAULT));
    if (!dataset)
        throw cae::FileFormatError("Failed to open HDF5 dataset: " + path);

    H5Handle dataspace(H5Dget_space(dataset.Get()));
    const int rank = H5Sget_simple_extent_ndims(dataspace.Get());
    std::vector<hsize_t> dims(static_cast<size_t>(std::max(rank, 0)));
    size_t count = 1;
    if (rank > 0)
    {
        H5Sget_simple_extent_dims(dataspace.Get(), dims.data(), nullptr);
        count = GetElementCount(dims);
    }

    std::vector<T> values(count);
    if (count == 0)
        return values;

    if (H5Dread(dataset.Get(), memType, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data()) < 0)
        throw cae::FileFormatError("Failed to read HDF5 dataset: " + path);
    return values;
}

static VtVec3fArray LoadVec3Array(const std::string& filePath, const std::string& datasetPath, const ReadOptions&)
{
    H5Handle file = OpenFileReadOnly(filePath);
    const DatasetInfo info = InspectDataset(file.Get(), datasetPath);
    size_t tupleCount = 0;
    if (info.dims.size() == 2 && info.dims[1] == 3)
        tupleCount = info.dims[0];
    else if (info.dims.size() == 1 && info.dims[0] % 3 == 0)
        tupleCount = info.dims[0] / 3;
    else
        throw cae::FileFormatError("EDEM dataset is not a vec3 array: " + datasetPath);

    UninitializedVtArray<GfVec3f> out = MakeUninitializedVtArray<GfVec3f>(tupleCount);
    std::unique_ptr<double[]> raw =
        ReadNumericDatasetBuffer<double>(file.Get(), datasetPath, H5T_NATIVE_DOUBLE, tupleCount * 3u);
    for (size_t i = 0; i < tupleCount; ++i)
        out.data[i] = GfVec3f(static_cast<float>(raw[i * 3u]), static_cast<float>(raw[i * 3u + 1u]),
                              static_cast<float>(raw[i * 3u + 2u]));
    return out.array;
}

static VtVec4fArray LoadVec4Array(const std::string& filePath, const std::string& datasetPath, const ReadOptions&)
{
    H5Handle file = OpenFileReadOnly(filePath);
    const DatasetInfo info = InspectDataset(file.Get(), datasetPath);
    size_t tupleCount = 0;
    if (info.dims.size() == 2 && info.dims[1] == 4)
        tupleCount = info.dims[0];
    else if (info.dims.size() == 1 && info.dims[0] % 4 == 0)
        tupleCount = info.dims[0] / 4;
    else
        throw cae::FileFormatError("EDEM dataset is not a vec4 array: " + datasetPath);

    UninitializedVtArray<GfVec4f> out = MakeUninitializedVtArray<GfVec4f>(tupleCount);
    std::unique_ptr<double[]> raw =
        ReadNumericDatasetBuffer<double>(file.Get(), datasetPath, H5T_NATIVE_DOUBLE, tupleCount * 4u);
    for (size_t i = 0; i < tupleCount; ++i)
        out.data[i] = GfVec4f(static_cast<float>(raw[i * 4u]), static_cast<float>(raw[i * 4u + 1u]),
                              static_cast<float>(raw[i * 4u + 2u]), static_cast<float>(raw[i * 4u + 3u]));
    return out.array;
}

static VtFloatArray LoadFloatArray(const std::string& filePath, const std::string& datasetPath, const ReadOptions&)
{
    H5Handle file = OpenFileReadOnly(filePath);
    const DatasetInfo info = InspectDataset(file.Get(), datasetPath);
    const size_t count = GetElementCount(info.dims);
    UninitializedVtArray<float> out = MakeUninitializedVtArray<float>(count);
    std::unique_ptr<double[]> raw = ReadNumericDatasetBuffer<double>(file.Get(), datasetPath, H5T_NATIVE_DOUBLE, count);
    for (size_t i = 0; i < count; ++i)
        out.data[i] = static_cast<float>(raw[i]);
    return out.array;
}

static VtIntArray LoadIntArray(const std::string& filePath, const std::string& datasetPath, const ReadOptions&)
{
    H5Handle file = OpenFileReadOnly(filePath);
    const DatasetInfo info = InspectDataset(file.Get(), datasetPath);
    const size_t count = GetElementCount(info.dims);
    UninitializedVtArray<int> out = MakeUninitializedVtArray<int>(count);
    std::unique_ptr<long long[]> raw =
        ReadNumericDatasetBuffer<long long>(file.Get(), datasetPath, H5T_NATIVE_LLONG, count);
    for (size_t i = 0; i < count; ++i)
        out.data[i] = static_cast<int>(raw[i]);
    return out.array;
}

static std::vector<SpherePrimitive> LoadSpherePrimitives(const std::string& filePath, const std::string& datasetPath)
{
    struct RawSpherePrimitive
    {
        std::array<char, 64> name;
        std::array<float, 3> pos;
        float physicalRadius;
        float contactRadius;
    };

    H5Handle file = OpenFileReadOnly(filePath);
    H5Handle dataset(H5Dopen2(file.Get(), datasetPath.c_str(), H5P_DEFAULT));
    if (!dataset)
        throw cae::FileFormatError("Failed to open HDF5 dataset: " + datasetPath);

    H5Handle fileSpace(H5Dget_space(dataset.Get()));
    if (!fileSpace)
        throw cae::FileFormatError("Failed to inspect HDF5 dataset: " + datasetPath);

    const int rank = H5Sget_simple_extent_ndims(fileSpace.Get());
    if (rank != 1)
        throw cae::FileFormatError("EDEM spheres dataset is not one-dimensional: " + datasetPath);

    hsize_t count = 0;
    if (H5Sget_simple_extent_dims(fileSpace.Get(), &count, nullptr) < 0)
        throw cae::FileFormatError("Failed to inspect EDEM spheres dataset dimensions: " + datasetPath);

    H5Handle stringType(H5Tcopy(H5T_C_S1));
    H5Tset_size(stringType.Get(), sizeof(RawSpherePrimitive::name));

    const std::array<hsize_t, 1> posDims = { 3 };
    H5Handle posArrayType(H5Tarray_create2(H5T_NATIVE_FLOAT, 1, posDims.data()));

    H5Handle memType(H5Tcreate(H5T_COMPOUND, sizeof(RawSpherePrimitive)));
    H5Tinsert(memType.Get(), "name", HOFFSET(RawSpherePrimitive, name), stringType.Get());
    H5Tinsert(memType.Get(), "pos", HOFFSET(RawSpherePrimitive, pos), posArrayType.Get());
    H5Tinsert(memType.Get(), "physicalRadius", HOFFSET(RawSpherePrimitive, physicalRadius), H5T_NATIVE_FLOAT);
    H5Tinsert(memType.Get(), "contactRadius", HOFFSET(RawSpherePrimitive, contactRadius), H5T_NATIVE_FLOAT);

    std::vector<RawSpherePrimitive> raw(count);
    if (count > 0 && H5Dread(dataset.Get(), memType.Get(), H5S_ALL, H5S_ALL, H5P_DEFAULT, raw.data()) < 0)
        throw cae::FileFormatError("Failed to read EDEM spheres dataset: " + datasetPath);

    std::vector<SpherePrimitive> spheres;
    spheres.reserve(raw.size());
    for (const RawSpherePrimitive& item : raw)
    {
        SpherePrimitive sphere;
        sphere.name = TrimNulls(std::string(item.name.data(), strnlen(item.name.data(), item.name.size())));
        if (sphere.name.empty())
            sphere.name = "Sphere";
        sphere.position = GfVec3f(item.pos[0], item.pos[1], item.pos[2]);
        sphere.physicalRadius = item.physicalRadius;
        sphere.contactRadius = item.contactRadius;
        spheres.push_back(std::move(sphere));
    }
    return spheres;
}

static void LoadMeshGeometry(const std::string& filePath,
                             const std::string& basePath,
                             const ReadOptions& options,
                             VtVec3fArray* points,
                             VtIntArray* indices,
                             VtIntArray* counts)
{
    if (!points || !indices || !counts)
        throw cae::FileFormatError("Invalid mesh geometry output pointers");

    *points = LoadVec3Array(filePath, basePath + "/coords", options);

    H5Handle file = OpenFileReadOnly(filePath);
    const DatasetInfo triInfo = InspectDataset(file.Get(), basePath + "/triangle nodes");
    size_t triangleCount = 0;
    if (triInfo.dims.size() == 2 && triInfo.dims[1] == 3)
        triangleCount = triInfo.dims[0];
    else if (triInfo.dims.size() == 1 && triInfo.dims[0] % 3 == 0)
        triangleCount = triInfo.dims[0] / 3;
    else
        throw cae::FileFormatError("EDEM triangle nodes dataset is not triangular: " + basePath);

    *indices = LoadIntArray(filePath, basePath + "/triangle nodes", options);
    *counts = VtIntArray(triangleCount, 3);
}

static GfMatrix4d LoadTransformMatrix(const std::string& filePath, const std::string& datasetPath)
{
    H5Handle file = OpenFileReadOnly(filePath);
    std::vector<double> raw = ReadNumericDataset<double>(file.Get(), datasetPath, H5T_NATIVE_DOUBLE);
    if (raw.size() != 16)
        throw cae::FileFormatError("EDEM transform dataset is not 4x4: " + datasetPath);

    GfMatrix4d matrix(1.0);
    size_t cursor = 0;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            matrix[row][col] = raw[cursor++];
    return matrix.GetTranspose();
}

static std::optional<FieldInfo> InspectField(const std::string& filePath,
                                             const std::string& datasetPath,
                                             const std::string& sourceName)
{
    H5Handle file = OpenFileReadOnly(filePath);
    const DatasetInfo info = InspectDataset(file.Get(), datasetPath);
    FieldInfo field;
    field.sourceName = sourceName;
    field.instanceName = TfToken(TfMakeValidIdentifier(sourceName));

    if (info.typeClass == H5T_INTEGER)
    {
        field.valueType = TfToken("int[]");
        return field;
    }
    if (info.typeClass != H5T_FLOAT)
        return std::nullopt;

    if (info.dims.size() == 2 && info.dims[1] == 3)
    {
        field.valueType = TfToken("float3[]");
        return field;
    }
    if (info.dims.size() == 2 && info.dims[1] == 4)
    {
        field.valueType = TfToken("float4[]");
        return field;
    }
    if (info.dims.size() == 1 || (info.dims.size() == 2 && info.dims[1] == 1))
    {
        field.valueType = TfToken("float[]");
        return field;
    }
    return std::nullopt;
}

static VtValue LoadFieldValue(const std::string& filePath,
                              const std::string& datasetPath,
                              const TfToken& valueType,
                              const ReadOptions& options)
{
    if (valueType == TfToken("int[]"))
    {
        VtIntArray values = LoadIntArray(filePath, datasetPath, options);
        return VtValue::Take(values);
    }
    if (valueType == TfToken("float[]"))
    {
        VtFloatArray values = LoadFloatArray(filePath, datasetPath, options);
        return VtValue::Take(values);
    }
    if (valueType == TfToken("float3[]"))
    {
        VtVec3fArray values = LoadVec3Array(filePath, datasetPath, options);
        return VtValue::Take(values);
    }
    if (valueType == TfToken("float4[]"))
    {
        VtVec4fArray values = LoadVec4Array(filePath, datasetPath, options);
        return VtValue::Take(values);
    }
    throw cae::FileFormatError("Unsupported EDEM field value type");
}

static ReadOptions ParseReadOptions(const std::string& fname, const SdfLayer::FileFormatArguments& args)
{
    auto getArg = [&](const TfToken& key) -> std::string
    {
        const auto it = args.find(key.GetString());
        return it != args.end() ? it->second : std::string{};
    };

    ReadOptions options;
    options.rootPath = CaeResolveRootPrimPath(fname, args);
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);

    const std::string timeScale = getArg(OmniSciEdemFileFormatTokens->ArgTimeScale);
    if (!timeScale.empty())
        options.timeScale = std::stod(timeScale);

    const std::string timeOffset = getArg(OmniSciEdemFileFormatTokens->ArgTimeOffset);
    if (!timeOffset.empty())
        options.timeOffset = std::stod(timeOffset);

    const std::string timeSource = getArg(OmniSciEdemFileFormatTokens->ArgTimeSource);
    if (!timeSource.empty())
        options.timeSource = timeSource;

    const std::string ioThreads = getArg(OmniSciEdemFileFormatTokens->ArgIoThreads);
    options.ioThreads = ioThreads.empty() ? 1 : std::max(1, std::stoi(ioThreads));

    return options;
}

static double ResolveSampleTime(const ReadOptions& options, size_t sampleIndex, double caseTime)
{
    const std::string source = TfStringToLower(options.timeSource);
    const double raw = (source == "timevalue") ? caseTime : static_cast<double>(sampleIndex);
    return raw * options.timeScale + options.timeOffset;
}

static std::vector<double> ResolveSampleTimes(const ReadOptions& options, const std::vector<TimeSampleInfo>& samples)
{
    std::vector<double> sampleTimes;
    sampleTimes.reserve(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
        sampleTimes.push_back(ResolveSampleTime(options, i, samples[i].caseTime));
    return sampleTimes;
}

static double GetSampleTime(const CaseInfo& caseInfo, size_t sampleIndex)
{
    return sampleIndex < caseInfo.sampleTimes.size() ?
               caseInfo.sampleTimes[sampleIndex] :
               ResolveSampleTime(caseInfo.options, sampleIndex, caseInfo.samples[sampleIndex].caseTime);
}

static std::vector<TimeSampleInfo> ScanTimeSamples(const std::string& dataDir, int numTimesteps)
{
    std::vector<TimeSampleInfo> samples;
    for (int i = 0; i < numTimesteps; ++i)
    {
        const fs::path filePath = fs::path(dataDir) / (std::to_string(i) + ".h5");
        if (!fs::is_regular_file(filePath))
            continue;

        H5Handle file = OpenFileReadOnly(filePath.string());
        const std::vector<std::string> timestepNodes = ListChildNames(file.Get(), "/TimestepData");
        for (const std::string& node : timestepNodes)
        {
            const std::string timestepPath = "/TimestepData/" + node;
            H5Handle timestepGroup(H5Gopen2(file.Get(), timestepPath.c_str(), H5P_DEFAULT));
            if (!timestepGroup)
                continue;

            TimeSampleInfo sample;
            sample.filePath = filePath.string();
            sample.timestepPath = timestepPath;
            sample.caseTime = ReadScalarAttr<double>(timestepGroup.Get(), "time", static_cast<double>(samples.size()));
            TF_DEBUG(CAE_EDEM_FILEFORMAT)
                .Msg("[EDEM] timestep sample file='%s' path='%s' caseTime=%g\n", sample.filePath.c_str(),
                     sample.timestepPath.c_str(), sample.caseTime);
            samples.push_back(std::move(sample));
        }
    }
    return samples;
}

static std::vector<ParticleTypeInfo> ScanParticleTypes(const std::string& firstSampleFile)
{
    std::vector<ParticleTypeInfo> out;
    H5Handle file = OpenFileReadOnly(firstSampleFile);
    const std::string rootPath = "/CreatorData/0/ParticleTypes";
    for (const std::string& node : ListChildNames(file.Get(), rootPath))
    {
        const std::string groupPath = rootPath + "/" + node;
        H5Handle group(H5Gopen2(file.Get(), groupPath.c_str(), H5P_DEFAULT));
        if (!group)
            continue;

        ParticleTypeInfo info;
        info.sourceNode = node;
        info.name = ReadStringAttr(group.Get(), "name", node);
        info.creatorPath = groupPath;
        if (PathExists(file.Get(), groupPath + "/coords") && PathExists(file.Get(), groupPath + "/triangle nodes"))
            info.kind = PrototypeKind::Polyhedral;
        else if (PathExists(file.Get(), groupPath + "/spheres"))
            info.kind = PrototypeKind::SphereCluster;
        const char* kindName = "unknown";
        if (info.kind == PrototypeKind::Polyhedral)
            kindName = "polyhedral";
        else if (info.kind == PrototypeKind::SphereCluster)
            kindName = "sphereCluster";
        TF_DEBUG(CAE_EDEM_FILEFORMAT)
            .Msg("[EDEM] particle type node='%s' name='%s' creatorPath='%s' kind=%s\n", info.sourceNode.c_str(),
                 info.name.c_str(), info.creatorPath.c_str(), kindName);
        out.push_back(std::move(info));
    }
    return out;
}

static std::vector<GeometryGroupInfo> ScanGeometryGroups(const std::string& firstSampleFile)
{
    std::vector<GeometryGroupInfo> out;
    H5Handle file = OpenFileReadOnly(firstSampleFile);
    const std::string rootPath = "/CreatorData/0/GeometryGroups";
    for (const std::string& node : ListChildNames(file.Get(), rootPath))
    {
        const std::string groupPath = rootPath + "/" + node;
        H5Handle group(H5Gopen2(file.Get(), groupPath.c_str(), H5P_DEFAULT));
        if (!group)
            continue;

        if (!PathExists(file.Get(), groupPath + "/coords") || !PathExists(file.Get(), groupPath + "/triangle nodes"))
            continue;

        GeometryGroupInfo info;
        info.sourceNode = node;
        info.name = ReadStringAttr(group.Get(), "name", node);
        info.creatorPath = groupPath;
        TF_DEBUG(CAE_EDEM_FILEFORMAT)
            .Msg("[EDEM] geometry group node='%s' name='%s' creatorPath='%s'\n", info.sourceNode.c_str(),
                 info.name.c_str(), info.creatorPath.c_str());
        out.push_back(std::move(info));
    }
    return out;
}

static std::vector<FieldInfo> ScanParticleFields(const ParticleTypeInfo& typeInfo, const TimeSampleInfo& firstSample)
{
    std::vector<FieldInfo> fields;
    H5Handle file = OpenFileReadOnly(firstSample.filePath);
    const std::string basePath = firstSample.timestepPath + "/ParticleTypes/" + typeInfo.sourceNode;

    // "position" is authored as the point cloud positions attribute, not as a generic field.
    static const std::array<std::string, 1> kReservedFields = { "position" };

    std::vector<std::string> children = ListChildNames(file.Get(), basePath);
    std::sort(children.begin(), children.end());
    for (const std::string& child : children)
    {
        if (std::find(kReservedFields.begin(), kReservedFields.end(), child) != kReservedFields.end())
            continue;

        const std::string childPath = basePath + "/" + child;
        if (!IsDataset(file.Get(), childPath))
        {
            TF_DEBUG(CAE_EDEM_FILEFORMAT)
                .Msg("[EDEM] skip non-dataset particle field '%s' under '%s'\n", child.c_str(), basePath.c_str());
            continue;
        }

        const auto info = InspectField(firstSample.filePath, childPath, child);
        if (info)
        {
            TF_DEBUG(CAE_EDEM_FILEFORMAT)
                .Msg("[EDEM] particle field name='%s' valueType='%s' dataset='%s'\n", info->sourceName.c_str(),
                     info->valueType.GetText(), childPath.c_str());
            fields.push_back(*info);
        }
        else
        {
            TF_DEBUG(CAE_EDEM_FILEFORMAT)
                .Msg("[EDEM] unsupported particle field '%s' dataset='%s'\n", child.c_str(), childPath.c_str());
        }
    }
    return fields;
}

static CaseInfo ParseCase(const std::string& casePath, const SdfLayer::FileFormatArguments& args)
{
    CaseInfo info;
    info.casePath = casePath;
    info.caseDir = fs::path(casePath).parent_path().string();
    info.options = ParseReadOptions(casePath, args);

    const fs::path caseStem = fs::path(casePath).stem();
    info.dataDir = (fs::path(info.caseDir) / (caseStem.string() + "_data")).string();
    TF_DEBUG(CAE_EDEM_FILEFORMAT)
        .Msg("[EDEM] ParseCase case='%s' dataDir='%s'\n", info.casePath.c_str(), info.dataDir.c_str());

    H5Handle deck = OpenFileReadOnly(casePath);
    const int numTimesteps = ReadScalarAttr<long long>(deck.Get(), "num timesteps", 0);
    if (numTimesteps <= 0)
        throw cae::FileFormatError("EDEM deck reports no timesteps: " + casePath);
    TF_DEBUG(CAE_EDEM_FILEFORMAT).Msg("[EDEM] numTimesteps=%d\n", numTimesteps);

    info.samples = ScanTimeSamples(info.dataDir, numTimesteps);
    if (info.samples.empty())
        throw cae::FileFormatError("EDEM deck has no readable timestep samples: " + casePath);
    info.sampleTimes = ResolveSampleTimes(info.options, info.samples);
    info.sampleTimesAreSorted = std::is_sorted(info.sampleTimes.begin(), info.sampleTimes.end());

    info.particleTypes = ScanParticleTypes(info.samples.front().filePath);
    info.geometryGroups = ScanGeometryGroups(info.samples.front().filePath);

    for (const ParticleTypeInfo& typeInfo : info.particleTypes)
    {
        ParticleCloudInfo cloud;
        cloud.particleType = typeInfo;
        cloud.fields = ScanParticleFields(typeInfo, info.samples.front());
        info.particleClouds.push_back(std::move(cloud));
    }

    TF_DEBUG(CAE_EDEM_FILEFORMAT)
        .Msg("[EDEM] parsed samples=%zu particleTypes=%zu geometryGroups=%zu particleClouds=%zu\n", info.samples.size(),
             info.particleTypes.size(), info.geometryGroups.size(), info.particleClouds.size());

    return info;
}

static bool ShouldAuthorStructure(const ReadContext& ctx)
{
    return ctx.mode != ReadMode::FileDataOnly;
}

static bool ShouldRegisterFileData(const ReadContext& ctx)
{
    return ctx.mode != ReadMode::StructureOnly;
}

static ReadContext CreateReadContext(const CaseInfo& caseInfo, ReadMode mode)
{
    ReadContext ctx;
    ctx.caseInfo = caseInfo;
    ctx.mode = mode;
    if (ShouldAuthorStructure(ctx))
    {
        ctx.layer = SdfLayer::CreateAnonymous();
        ctx.stage = UsdStage::Open(ctx.layer);
        UsdGeomSetStageUpAxis(ctx.stage, UsdGeomTokens->z);
        // Canonical simulation-seconds timecodes: self-describe the unit so
        // direct Stage.Open plays at real-time and composition tooling can
        // reconcile host-stage TCPS through SdfLayerOffset.
        ctx.stage->SetTimeCodesPerSecond(1.0);
    }
    if (ShouldRegisterFileData(ctx))
        ctx.fileData = CreateCaeFileFormatData(ctx.caseInfo.options.cacheMode);
    return ctx;
}

static void RegisterTimeSamples(ReadContext& ctx,
                                const SdfPath& primPath,
                                const TfToken& attrName,
                                const TfToken& typeName,
                                std::vector<std::pair<double, CaeFileFormatData::Loader>> samples)
{
    if (ctx.caseInfo.sampleTimesAreSorted)
        ctx.fileData->RegisterLazyTimeSamplesSorted(primPath, attrName, typeName, std::move(samples));
    else
        ctx.fileData->RegisterLazyTimeSamples(primPath, attrName, typeName, std::move(samples));
}

static CaeFileFormatData::Loader MakePositionsLoader(const TimeSampleInfo& sample,
                                                     const ParticleTypeInfo& typeInfo,
                                                     const ReadOptions& options)
{
    const std::string datasetPath = sample.timestepPath + "/ParticleTypes/" + typeInfo.sourceNode + "/position";
    return [filePath = sample.filePath, datasetPath, options]()
    {
        TF_DEBUG(CAE_EDEM_FILEFORMAT)
            .Msg("[EDEM] lazy load positions file='%s' dataset='%s'\n", filePath.c_str(), datasetPath.c_str());
        VtVec3fArray values = LoadVec3Array(filePath, datasetPath, options);
        return VtValue::Take(values);
    };
}

static CaeFileFormatData::Loader MakeFieldLoader(const TimeSampleInfo& sample,
                                                 const ParticleTypeInfo& typeInfo,
                                                 const FieldInfo& field,
                                                 const ReadOptions& options)
{
    const std::string datasetPath =
        sample.timestepPath + "/ParticleTypes/" + typeInfo.sourceNode + "/" + field.sourceName;
    return [filePath = sample.filePath, datasetPath, valueType = field.valueType, options]()
    {
        TF_DEBUG(CAE_EDEM_FILEFORMAT)
            .Msg("[EDEM] lazy load field file='%s' dataset='%s' valueType='%s'\n", filePath.c_str(),
                 datasetPath.c_str(), valueType.GetText());
        return LoadFieldValue(filePath, datasetPath, valueType, options);
    };
}

static ReadEdemResult ReadEdem(const std::string& casePath, const SdfLayer::FileFormatArguments& args, ReadMode mode)
{
    const CaseInfo caseInfo = ParseCase(casePath, args);
    ReadContext ctx = CreateReadContext(caseInfo, mode);
    const bool authorStructure = ShouldAuthorStructure(ctx);
    const bool registerFileData = ShouldRegisterFileData(ctx);
    TF_DEBUG(CAE_EDEM_FILEFORMAT)
        .Msg("[EDEM] ReadEdem rootPath='%s' timeSource='%s'\n", caseInfo.options.rootPath.GetAsString().c_str(),
             caseInfo.options.timeSource.c_str());

    const SdfPath rootPath = caseInfo.options.rootPath;
    if (authorStructure)
        UsdGeomScope::Define(ctx.stage, rootPath);

    const SdfPath geometryGroupsPath = rootPath.AppendChild(TfToken("GeometryGroups"));
    const SdfPath particleTypesPath = rootPath.AppendChild(TfToken("ParticleTypes"));
    const SdfPath particlesPath = rootPath.AppendChild(TfToken("Particles"));
    if (authorStructure)
    {
        UsdGeomScope::Define(ctx.stage, geometryGroupsPath);
        UsdGeomScope::Define(ctx.stage, particleTypesPath);
        UsdGeomScope::Define(ctx.stage, particlesPath);
    }

    if (authorStructure)
    {
        for (const GeometryGroupInfo& groupInfo : caseInfo.geometryGroups)
        {
            const TfToken primName(TfMakeValidIdentifier(groupInfo.name));
            const SdfPath meshPath = geometryGroupsPath.AppendChild(primName);
            UsdGeomMesh mesh = UsdGeomMesh::Define(ctx.stage, meshPath);

            VtVec3fArray points;
            VtIntArray indices;
            VtIntArray counts;
            LoadMeshGeometry(
                caseInfo.samples.front().filePath, groupInfo.creatorPath, caseInfo.options, &points, &indices, &counts);
            mesh.CreatePointsAttr().Set(points);
            mesh.CreateFaceVertexIndicesAttr().Set(indices);
            mesh.CreateFaceVertexCountsAttr().Set(counts);

            OmniSciEdemGeometryGroupAPI api = OmniSciEdemGeometryGroupAPI::Apply(mesh.GetPrim());
            api.CreateNameAttr().Set(groupInfo.name);
            api.CreateSourceNodeAttr().Set(groupInfo.sourceNode);
            TF_DEBUG(CAE_EDEM_FILEFORMAT)
                .Msg("[EDEM] authored geometry group prim='%s' sourceNode='%s'\n", meshPath.GetText(),
                     groupInfo.sourceNode.c_str());

            UsdGeomXformOp transformOp = mesh.AddTransformOp();
            for (size_t i = 0; i < caseInfo.samples.size(); ++i)
            {
                const TimeSampleInfo& sample = caseInfo.samples[i];
                const std::string transformPath =
                    sample.timestepPath + "/GeometryGroups/" + groupInfo.sourceNode + "/Kinematics/0/global transform";
                try
                {
                    H5Handle file = OpenFileReadOnly(sample.filePath);
                    if (!PathExists(file.Get(), transformPath))
                        continue;
                    transformOp.Set(LoadTransformMatrix(sample.filePath, transformPath), GetSampleTime(caseInfo, i));
                }
                catch (const std::exception&)
                {
                    // Ignore missing/unsupported transforms in v1.
                }
            }
        }
    }

    if (authorStructure)
    {
        for (const ParticleTypeInfo& typeInfo : caseInfo.particleTypes)
        {
            const TfToken primName(TfMakeValidIdentifier(typeInfo.name));
            const SdfPath prototypePath = particleTypesPath.AppendChild(primName);
            UsdPrim prototypePrim;
            if (typeInfo.kind == PrototypeKind::Polyhedral)
            {
                UsdGeomMesh mesh = UsdGeomMesh::Define(ctx.stage, prototypePath);
                VtVec3fArray points;
                VtIntArray indices;
                VtIntArray counts;
                LoadMeshGeometry(caseInfo.samples.front().filePath, typeInfo.creatorPath, caseInfo.options, &points,
                                 &indices, &counts);
                mesh.CreatePointsAttr().Set(points);
                mesh.CreateFaceVertexIndicesAttr().Set(indices);
                mesh.CreateFaceVertexCountsAttr().Set(counts);
                prototypePrim = mesh.GetPrim();
            }
            else if (typeInfo.kind == PrototypeKind::SphereCluster)
            {
                UsdGeomXform xform = UsdGeomXform::Define(ctx.stage, prototypePath);
                prototypePrim = xform.GetPrim();

                const std::vector<SpherePrimitive> spheres =
                    LoadSpherePrimitives(caseInfo.samples.front().filePath, typeInfo.creatorPath + "/spheres");
                TF_DEBUG(CAE_EDEM_FILEFORMAT)
                    .Msg("[EDEM] sphere cluster prototype='%s' sphereCount=%zu\n", prototypePath.GetText(),
                         spheres.size());

                for (const SpherePrimitive& sphereInfo : spheres)
                {
                    const SdfPath spherePath = prototypePath.AppendChild(TfToken(TfMakeValidIdentifier(sphereInfo.name)));
                    UsdGeomSphere sphere = UsdGeomSphere::Define(ctx.stage, spherePath);
                    sphere.CreateRadiusAttr().Set(static_cast<double>(sphereInfo.physicalRadius));
                    sphere.AddTranslateOp().Set(
                        GfVec3d(sphereInfo.position[0], sphereInfo.position[1], sphereInfo.position[2]));
                }
            }
            else
            {
                prototypePrim = UsdGeomXform::Define(ctx.stage, prototypePath).GetPrim();
            }

            OmniSciEdemParticleTypeAPI api = OmniSciEdemParticleTypeAPI::Apply(prototypePrim);
            api.CreateNameAttr().Set(typeInfo.name);
            api.CreateSourceNodeAttr().Set(typeInfo.sourceNode);
            api.CreateShapeKindAttr().Set(typeInfo.kind == PrototypeKind::Polyhedral    ? TfToken("polyhedral") :
                                          typeInfo.kind == PrototypeKind::SphereCluster ? TfToken("sphereCluster") :
                                                                                          TfToken("unknown"));
            TF_DEBUG(CAE_EDEM_FILEFORMAT)
                .Msg("[EDEM] authored particle type prim='%s' sourceNode='%s'\n", prototypePath.GetText(),
                     typeInfo.sourceNode.c_str());
        }
    }

    for (const ParticleCloudInfo& cloudInfo : caseInfo.particleClouds)
    {
        const TfToken primName(TfMakeValidIdentifier(cloudInfo.particleType.name));
        const SdfPath cloudPath = particlesPath.AppendChild(primName);
        UsdPrim cloudPrim;
        if (authorStructure)
        {
            cloudPrim = OmniSciDataset::Define(ctx.stage, cloudPath).GetPrim();
            OmniSciCaePointCloudAPI::Apply(cloudPrim);

            OmniSciEdemParticleCloudAPI cloudAPI = OmniSciEdemParticleCloudAPI::Apply(cloudPrim);
            cloudAPI.CreateNameAttr().Set(cloudInfo.particleType.name);
            cloudAPI.CreateSourceNodeAttr().Set(cloudInfo.particleType.sourceNode);
            cloudAPI.CreatePrototypeRel().SetTargets({ particleTypesPath.AppendChild(primName) });
            TF_DEBUG(CAE_EDEM_FILEFORMAT)
                .Msg("[EDEM] authored particle cloud prim='%s' prototype='%s' fieldCount=%zu\n", cloudPath.GetText(),
                     particleTypesPath.AppendChild(primName).GetText(), cloudInfo.fields.size());

            OmniSciArrayAPI pointsArray = OmniSciArrayAPI::Apply(cloudPrim, OmniSciCaeTokens->points);
            pointsArray.CreateDeviceAttr().Set(TfToken("cpu"));
        }

        if (registerFileData && caseInfo.samples.size() > 1)
        {
            std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
            samples.reserve(caseInfo.samples.size());
            for (size_t i = 0; i < caseInfo.samples.size(); ++i)
                samples.push_back({ GetSampleTime(caseInfo, i),
                                    MakePositionsLoader(caseInfo.samples[i], cloudInfo.particleType, caseInfo.options) });
            RegisterTimeSamples(ctx, cloudPath, MakeArrayValueAttrName(OmniSciCaeTokens->points), TfToken("float3[]"),
                                std::move(samples));
        }
        else if (registerFileData)
        {
            ctx.fileData->RegisterLazySingleState(
                cloudPath, MakeArrayValueAttrName(OmniSciCaeTokens->points), TfToken("float3[]"),
                GetSampleTime(caseInfo, 0),
                MakePositionsLoader(caseInfo.samples.front(), cloudInfo.particleType, caseInfo.options));
        }

        for (const FieldInfo& fieldInfo : cloudInfo.fields)
        {
            if (authorStructure)
            {
                OmniSciFieldAPI fieldAPI = OmniSciFieldAPI::Apply(cloudPrim, fieldInfo.instanceName);
                fieldAPI.CreateNameAttr().Set(fieldInfo.sourceName);
                fieldAPI.CreateAssociationAttr().Set(TfToken("node"));

                OmniSciArrayAPI arrayAPI = OmniSciArrayAPI::Apply(cloudPrim, fieldInfo.instanceName);
                arrayAPI.CreateDeviceAttr().Set(TfToken("cpu"));
            }

            if (registerFileData && caseInfo.samples.size() > 1)
            {
                std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
                samples.reserve(caseInfo.samples.size());
                for (size_t i = 0; i < caseInfo.samples.size(); ++i)
                    samples.push_back(
                        { GetSampleTime(caseInfo, i),
                          MakeFieldLoader(caseInfo.samples[i], cloudInfo.particleType, fieldInfo, caseInfo.options) });
                RegisterTimeSamples(ctx, cloudPath, MakeArrayValueAttrName(fieldInfo.instanceName), fieldInfo.valueType,
                                    std::move(samples));
            }
            else if (registerFileData)
            {
                ctx.fileData->RegisterLazySingleState(
                    cloudPath, MakeArrayValueAttrName(fieldInfo.instanceName), fieldInfo.valueType,
                    GetSampleTime(caseInfo, 0),
                    MakeFieldLoader(caseInfo.samples.front(), cloudInfo.particleType, fieldInfo, caseInfo.options));
            }
        }
    }

    return { ctx.layer, ctx.fileData };
}

} // namespace detail

OmniSciEdemFileFormat::OmniSciEdemFileFormat()
    : SdfFileFormat(OmniSciEdemFileFormatTokens->Id,
                    OmniSciEdemFileFormatTokens->Version,
                    OmniSciEdemFileFormatTokens->Target,
                    OmniSciEdemFileFormatTokens->Extension)
{
}

OmniSciEdemFileFormat::~OmniSciEdemFileFormat() = default;

bool OmniSciEdemFileFormat::CanRead(const std::string& filePath) const
{
    if (TfGetExtension(filePath) != OmniSciEdemFileFormatTokens->Extension)
    {
        TF_DEBUG(CAE_EDEM_FILEFORMAT)
            .Msg("OmniSciEdemFileFormat::CanRead('%s') -> false (extension mismatch)\n", filePath.c_str());
        return false;
    }
    if (!CaeCanScanAdjacentFiles(filePath, ArResolvedPath(filePath)))
        return false;

    const fs::path path(filePath);
    const fs::path dataDir = path.parent_path() / (path.stem().string() + "_data");
    const bool hasDeck = fs::exists(filePath);
    const bool hasDataDir = fs::is_directory(dataDir);
    const bool hasFirstSample = fs::exists(dataDir / "0.h5");
    const bool result = hasDeck && hasDataDir && hasFirstSample;
    TF_DEBUG(CAE_EDEM_FILEFORMAT)
        .Msg("OmniSciEdemFileFormat::CanRead('%s') -> %d deck=%d dataDir=%d firstSample=%d\n", filePath.c_str(),
             result ? 1 : 0, hasDeck ? 1 : 0, hasDataDir ? 1 : 0, hasFirstSample ? 1 : 0);
    return result;
}

bool OmniSciEdemFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool /*metadataOnly*/) const
{
    TF_DEBUG(CAE_EDEM_FILEFORMAT).Msg("OmniSciEdemFileFormat::Read('%s')\n", resolvedPath.c_str());

    if (!TF_VERIFY(layer))
        return false;

    const auto& fmtArgs = layer->GetFileFormatArguments();
    const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
    try
    {
        CaeRequireAdjacentFileScanning("EDEM", identifier, ArResolvedPath(resolvedPath));
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciEdemFileFormat: %s", ex.what());
        return false;
    }
    const auto readArgs = CaePrepareResolverArguments(identifier, fmtArgs);
    auto result = detail::ReadEdem(resolvedPath, readArgs, detail::ReadMode::StructureAndFileData);
    if (!result.first || !result.second)
        return false;

    result.second->CopyFrom(_GetLayerData(*result.first));
    SdfAbstractDataRefPtr fileData = result.second;
    _SetLayerData(layer, fileData);

    SdfPath rootPath;
    try
    {
        rootPath = CaeResolveRootPrimPath(identifier, fmtArgs);
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciEdemFileFormat: %s", ex.what());
        return false;
    }
    CaeAuthorMountPathOvers(layer, rootPath);
    return true;
}

void OmniSciEdemFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                const PcpDynamicFileFormatContext& context,
                                                                FileFormatArguments* args,
                                                                VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciEdemFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, detail::GetDynamicFileFormatArgs());
}

bool OmniSciEdemFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciEdemFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciEdemFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciEdemFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
