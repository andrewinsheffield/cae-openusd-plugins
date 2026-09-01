// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciVtkHdfFileFormat.h"

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
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <hdf5.h>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace fs = std::filesystem;

namespace detail
{

// -------------------------------------------------------------------------
// Dynamic file format arguments
// -------------------------------------------------------------------------

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 5> DynamicFileFormatArgs = { {
        { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciVtkHdfFileFormatTokens->ArgCacheMode },
        { OmniSciFileFormatArgsTokens->omniCaeFormatTimeScale, OmniSciVtkHdfFileFormatTokens->ArgTimeScale },
        { OmniSciFileFormatArgsTokens->omniCaeFormatTimeOffset, OmniSciVtkHdfFileFormatTokens->ArgTimeOffset },
        { OmniSciFileFormatArgsTokens->omniCaeFormatTimeSource, OmniSciVtkHdfFileFormatTokens->ArgTimeSource },
        { OmniSciFileFormatArgsTokens->omniCaeFormatStreamingIoThreads, OmniSciVtkHdfFileFormatTokens->ArgIoThreads },
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

enum class ReadMode
{
    StructureOnly,
    FileDataOnly,
    StructureAndFileData,
};

struct TimeSampleInfo
{
    std::string filePath;   // absolute path to one _t_N.vtkhdf file
    int stepIndex = 0;      // parsed integer from filename
    double caseTime = 0.0;  // TimeValue extracted from the file (or fallback = stepIndex)
};

struct AssemblyLink
{
    std::string name;       // link name under Assembly/<Category>
    std::string blockPath;  // absolute HDF5 path, e.g. "/VTKHDF/block_001"
};

struct TemplateInfo
{
    std::string name;         // e.g. "Paired"
    std::string blockPath;    // path used in the template source file
    std::string sourceFile;   // where the prototype geometry lives
};

struct FieldInfo
{
    std::string sourceName;     // dataset name, e.g. "velocity"
    TfToken instanceName;       // sanitized USD identifier
    TfToken valueType;          // "float[]" / "float3[]" / "float4[]" / "int[]"
};

struct GeometryInfo
{
    std::string name;
    std::string blockPath;
};

struct ParticleCloudInfo
{
    std::string name;              // matches a template name
    std::string blockPath;          // "/VTKHDF/block_XXX" as seen in first sample
    std::vector<FieldInfo> fields; // discovered on a non-empty sample
};

struct CaseInfo
{
    std::string casePath;                       // file the user opened
    std::string caseDir;
    ReadOptions options;
    std::vector<TimeSampleInfo> samples;         // sorted by stepIndex
    std::vector<double> sampleTimes;             // resolved through timeScale/timeOffset/timeSource
    bool sampleTimesAreSorted = true;

    // Prototype meshes (loaded once from a "template" source file).
    std::vector<TemplateInfo> templates;

    // Time-varying particle clouds (per template name).
    std::vector<ParticleCloudInfo> particleClouds;

    // Static geometry meshes (topology taken from the first sample).
    std::vector<GeometryInfo> geometries;
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

using ReadResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;

static TfToken MakeArrayValueAttrName(const TfToken& arrayName)
{
    return TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

// -------------------------------------------------------------------------
// HDF5 RAII helpers (mirrored from the EDEM plugin -- kept local to avoid
// a hard dependency on that plugin's internals).
// -------------------------------------------------------------------------

class H5Handle
{
public:
    H5Handle() = default;
    explicit H5Handle(hid_t id) : _id(id) {}
    H5Handle(const H5Handle&) = delete;
    H5Handle& operator=(const H5Handle&) = delete;
    H5Handle(H5Handle&& other) noexcept : _id(other._id) { other._id = -1; }
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
    ~H5Handle() { Reset(); }

    hid_t Get() const { return _id; }
    explicit operator bool() const { return _id >= 0; }

    void Reset()
    {
        if (_id < 0)
            return;
        const H5I_type_t type = H5Iget_type(_id);
        switch (type)
        {
        case H5I_FILE: H5Fclose(_id); break;
        case H5I_GROUP: H5Gclose(_id); break;
        case H5I_DATASET: H5Dclose(_id); break;
        case H5I_DATASPACE: H5Sclose(_id); break;
        case H5I_DATATYPE: H5Tclose(_id); break;
        case H5I_ATTR: H5Aclose(_id); break;
        default: break;
        }
        _id = -1;
    }

private:
    hid_t _id = -1;
};

struct DatasetInfo
{
    H5T_class_t typeClass = H5T_NO_CLASS;
    size_t elementSize = 0;
    std::vector<hsize_t> dims;
};

static H5Handle OpenFileReadOnly(const std::string& path)
{
    hid_t id = -1;
    H5E_BEGIN_TRY { id = H5Fopen(path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT); } H5E_END_TRY;
    H5Handle file(id);
    if (!file)
        throw cae::FileFormatError("Failed to open VTKHDF file: " + path);
    return file;
}

static bool PathExists(hid_t loc, const std::string& path)
{
    htri_t exists = 0;
    H5E_BEGIN_TRY { exists = H5Lexists(loc, path.c_str(), H5P_DEFAULT); } H5E_END_TRY;
    return exists > 0;
}

static std::vector<std::string> ListChildNames(hid_t loc, const std::string& path)
{
    std::vector<std::string> out;
    hid_t groupId = -1;
    H5E_BEGIN_TRY { groupId = H5Gopen2(loc, path.c_str(), H5P_DEFAULT); } H5E_END_TRY;
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
        while (!name.empty() && name.back() == '\0')
            name.pop_back();
        out.push_back(std::move(name));
    }
    return out;
}

// Resolve a soft link at loc + "/<parent>/<name>" to its absolute HDF5 target
// path.  Returns the joined path if the link is a hard link.
static std::string ReadLinkTarget(hid_t loc, const std::string& parentPath, const std::string& name)
{
    H5L_info_t linfo{};
    if (H5Lget_info(loc, (parentPath + "/" + name).c_str(), &linfo, H5P_DEFAULT) < 0)
        return {};

    if (linfo.type == H5L_TYPE_SOFT)
    {
        std::vector<char> buf(linfo.u.val_size + 1u, '\0');
        if (H5Lget_val(loc, (parentPath + "/" + name).c_str(), buf.data(), buf.size(), H5P_DEFAULT) < 0)
            return {};
        return std::string(buf.data());
    }
    return parentPath + "/" + name;
}

static DatasetInfo InspectDataset(hid_t loc, const std::string& path)
{
    hid_t datasetId = -1;
    H5E_BEGIN_TRY { datasetId = H5Dopen2(loc, path.c_str(), H5P_DEFAULT); } H5E_END_TRY;
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

    return { H5Tget_class(datatype.Get()), H5Tget_size(datatype.Get()), std::move(dims) };
}

static size_t ElementCount(const std::vector<hsize_t>& dims)
{
    size_t count = 1;
    for (hsize_t d : dims)
        count *= d;
    return count;
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
        count = ElementCount(dims);
    }
    std::vector<T> values(count);
    if (count == 0)
        return values;
    if (H5Dread(dataset.Get(), memType, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data()) < 0)
        throw cae::FileFormatError("Failed to read HDF5 dataset: " + path);
    return values;
}

// -------------------------------------------------------------------------
// Typed loaders
// -------------------------------------------------------------------------

static VtVec3fArray LoadVec3ArrayF32(const std::string& file, const std::string& path)
{
    H5Handle f = OpenFileReadOnly(file);
    const DatasetInfo info = InspectDataset(f.Get(), path);
    size_t tuples = 0;
    if (info.dims.size() == 2 && info.dims[1] == 3)
        tuples = info.dims[0];
    else if (info.dims.size() == 1 && info.dims[0] % 3 == 0)
        tuples = info.dims[0] / 3;
    else
        throw cae::FileFormatError("VTKHDF dataset is not a vec3 array: " + path);

    UninitializedVtArray<GfVec3f> out = MakeUninitializedVtArray<GfVec3f>(tuples);
    std::vector<float> raw(tuples * 3);
    if (tuples && H5Dread(H5Handle(H5Dopen2(f.Get(), path.c_str(), H5P_DEFAULT)).Get(), H5T_NATIVE_FLOAT, H5S_ALL,
                          H5S_ALL, H5P_DEFAULT, raw.data()) < 0)
        throw cae::FileFormatError("Failed to read HDF5 dataset: " + path);
    for (size_t i = 0; i < tuples; ++i)
        out.data[i] = GfVec3f(raw[3 * i], raw[3 * i + 1], raw[3 * i + 2]);
    return out.array;
}

static VtVec3fArray LoadVec3ArrayF64(const std::string& file, const std::string& path)
{
    H5Handle f = OpenFileReadOnly(file);
    const DatasetInfo info = InspectDataset(f.Get(), path);
    size_t tuples = 0;
    if (info.dims.size() == 2 && info.dims[1] == 3)
        tuples = info.dims[0];
    else if (info.dims.size() == 1 && info.dims[0] % 3 == 0)
        tuples = info.dims[0] / 3;
    else
        throw cae::FileFormatError("VTKHDF dataset is not a vec3 array: " + path);

    UninitializedVtArray<GfVec3f> out = MakeUninitializedVtArray<GfVec3f>(tuples);
    if (tuples == 0)
        return out.array;
    std::vector<double> raw(tuples * 3);
    if (H5Dread(H5Handle(H5Dopen2(f.Get(), path.c_str(), H5P_DEFAULT)).Get(), H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL,
                H5P_DEFAULT, raw.data()) < 0)
        throw cae::FileFormatError("Failed to read HDF5 dataset: " + path);
    for (size_t i = 0; i < tuples; ++i)
        out.data[i] = GfVec3f(static_cast<float>(raw[3 * i]), static_cast<float>(raw[3 * i + 1]),
                              static_cast<float>(raw[3 * i + 2]));
    return out.array;
}

static VtVec3fArray LoadPoints(const std::string& file, const std::string& blockPath)
{
    // Prefer whichever floating-point width the file uses.
    H5Handle f = OpenFileReadOnly(file);
    const std::string path = blockPath + "/Points";
    const DatasetInfo info = InspectDataset(f.Get(), path);
    if (info.elementSize <= 4)
        return LoadVec3ArrayF32(file, path);
    return LoadVec3ArrayF64(file, path);
}

static VtVec4fArray LoadVec4Array(const std::string& file, const std::string& path)
{
    H5Handle f = OpenFileReadOnly(file);
    const DatasetInfo info = InspectDataset(f.Get(), path);
    size_t tuples = 0;
    if (info.dims.size() == 2 && info.dims[1] == 4)
        tuples = info.dims[0];
    else if (info.dims.size() == 1 && info.dims[0] % 4 == 0)
        tuples = info.dims[0] / 4;
    else
        throw cae::FileFormatError("VTKHDF dataset is not a vec4 array: " + path);

    UninitializedVtArray<GfVec4f> out = MakeUninitializedVtArray<GfVec4f>(tuples);
    if (tuples == 0)
        return out.array;
    std::vector<double> raw(tuples * 4);
    if (H5Dread(H5Handle(H5Dopen2(f.Get(), path.c_str(), H5P_DEFAULT)).Get(), H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL,
                H5P_DEFAULT, raw.data()) < 0)
        throw cae::FileFormatError("Failed to read HDF5 dataset: " + path);
    for (size_t i = 0; i < tuples; ++i)
        out.data[i] = GfVec4f(static_cast<float>(raw[4 * i]), static_cast<float>(raw[4 * i + 1]),
                              static_cast<float>(raw[4 * i + 2]), static_cast<float>(raw[4 * i + 3]));
    return out.array;
}

static VtFloatArray LoadFloatArray(const std::string& file, const std::string& path)
{
    H5Handle f = OpenFileReadOnly(file);
    const DatasetInfo info = InspectDataset(f.Get(), path);
    const size_t count = ElementCount(info.dims);
    UninitializedVtArray<float> out = MakeUninitializedVtArray<float>(count);
    if (count == 0)
        return out.array;
    std::vector<double> raw(count);
    if (H5Dread(H5Handle(H5Dopen2(f.Get(), path.c_str(), H5P_DEFAULT)).Get(), H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL,
                H5P_DEFAULT, raw.data()) < 0)
        throw cae::FileFormatError("Failed to read HDF5 dataset: " + path);
    for (size_t i = 0; i < count; ++i)
        out.data[i] = static_cast<float>(raw[i]);
    return out.array;
}

static VtIntArray LoadIntArray(const std::string& file, const std::string& path)
{
    H5Handle f = OpenFileReadOnly(file);
    const DatasetInfo info = InspectDataset(f.Get(), path);
    const size_t count = ElementCount(info.dims);
    UninitializedVtArray<int> out = MakeUninitializedVtArray<int>(count);
    if (count == 0)
        return out.array;
    std::vector<long long> raw(count);
    if (H5Dread(H5Handle(H5Dopen2(f.Get(), path.c_str(), H5P_DEFAULT)).Get(), H5T_NATIVE_LLONG, H5S_ALL, H5S_ALL,
                H5P_DEFAULT, raw.data()) < 0)
        throw cae::FileFormatError("Failed to read HDF5 dataset: " + path);
    for (size_t i = 0; i < count; ++i)
        out.data[i] = static_cast<int>(raw[i]);
    return out.array;
}

// -------------------------------------------------------------------------
// PolyData mesh loading (Points + Polygons connectivity/offsets)
// -------------------------------------------------------------------------

static void LoadPolyDataMesh(const std::string& file,
                             const std::string& blockPath,
                             VtVec3fArray* points,
                             VtIntArray* faceVertexIndices,
                             VtIntArray* faceVertexCounts)
{
    if (!points || !faceVertexIndices || !faceVertexCounts)
        throw cae::FileFormatError("Invalid mesh output pointers");

    *points = LoadPoints(file, blockPath);

    H5Handle f = OpenFileReadOnly(file);
    const std::string offsetsPath = blockPath + "/Polygons/Offsets";
    const std::string connectivityPath = blockPath + "/Polygons/Connectivity";
    if (!PathExists(f.Get(), offsetsPath) || !PathExists(f.Get(), connectivityPath))
    {
        // Empty PolyData (e.g. particle template with no polygons at this timestep).
        faceVertexIndices->clear();
        faceVertexCounts->clear();
        return;
    }

    std::vector<long long> offsets = ReadNumericDataset<long long>(f.Get(), offsetsPath, H5T_NATIVE_LLONG);
    std::vector<long long> connectivity =
        ReadNumericDataset<long long>(f.Get(), connectivityPath, H5T_NATIVE_LLONG);

    if (offsets.size() < 2)
    {
        faceVertexIndices->clear();
        faceVertexCounts->clear();
        return;
    }

    const size_t cellCount = offsets.size() - 1u;
    UninitializedVtArray<int> counts = MakeUninitializedVtArray<int>(cellCount);
    for (size_t i = 0; i < cellCount; ++i)
        counts.data[i] = static_cast<int>(offsets[i + 1] - offsets[i]);
    *faceVertexCounts = std::move(counts.array);

    UninitializedVtArray<int> indices = MakeUninitializedVtArray<int>(connectivity.size());
    for (size_t i = 0; i < connectivity.size(); ++i)
        indices.data[i] = static_cast<int>(connectivity[i]);
    *faceVertexIndices = std::move(indices.array);
}

// -------------------------------------------------------------------------
// Sample scanning & option parsing
// -------------------------------------------------------------------------

// Parses a filename like "simulation_t_42" into (base="simulation", step=42).
static bool ParseTimestepStem(const std::string& stem, std::string* basePart, int* step);

// Ensures the mount-path arg is set to the timestep-suffix-stripped base name
// so every frame of a `<base>_t_<N>.vtkhdf` series composes onto the same root.
static SdfLayer::FileFormatArguments PatchMountPath(const std::string& fname,
                                                   const SdfLayer::FileFormatArguments& args)
{
    SdfLayer::FileFormatArguments patched = args;
    const auto it = patched.find(CaeMountPathArgName());
    if (it != patched.end() && !it->second.empty())
        return patched;

    const std::string stem = fs::path(fname).stem().string();
    std::string base;
    int step = 0;
    if (!ParseTimestepStem(stem, &base, &step) || base.empty())
        return patched;

    const std::string ident = TfMakeValidIdentifier(base);
    if (ident.empty())
        return patched;

    patched[CaeMountPathArgName()] = "/" + ident;
    return patched;
}

static ReadOptions ParseReadOptions(const std::string& fname, const SdfLayer::FileFormatArguments& args)
{
    auto getArg = [&](const TfToken& key) -> std::string
    {
        const auto it = args.find(key.GetString());
        return it != args.end() ? it->second : std::string{};
    };

    const SdfLayer::FileFormatArguments patchedArgs = PatchMountPath(fname, args);

    ReadOptions options;
    options.rootPath = CaeResolveRootPrimPath(fname, patchedArgs);
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);

    const std::string timeScale = getArg(OmniSciVtkHdfFileFormatTokens->ArgTimeScale);
    if (!timeScale.empty())
        options.timeScale = std::stod(timeScale);

    const std::string timeOffset = getArg(OmniSciVtkHdfFileFormatTokens->ArgTimeOffset);
    if (!timeOffset.empty())
        options.timeOffset = std::stod(timeOffset);

    const std::string timeSource = getArg(OmniSciVtkHdfFileFormatTokens->ArgTimeSource);
    if (!timeSource.empty())
        options.timeSource = timeSource;

    const std::string ioThreads = getArg(OmniSciVtkHdfFileFormatTokens->ArgIoThreads);
    options.ioThreads = ioThreads.empty() ? 1 : std::max(1, std::stoi(ioThreads));
    return options;
}

static double ResolveSampleTime(const ReadOptions& options, int stepIndex, double caseTime)
{
    const std::string src = TfStringToLower(options.timeSource);
    const double raw = (src == "timevalue") ? caseTime : static_cast<double>(stepIndex);
    return raw * options.timeScale + options.timeOffset;
}

// Parse a filename like "simulation_t_42.vtkhdf" into (base="simulation", step=42).
// Returns false if no "_t_<int>" suffix is present.
static bool ParseTimestepStem(const std::string& stem, std::string* basePart, int* step)
{
    const auto pos = stem.rfind("_t_");
    if (pos == std::string::npos)
        return false;
    const std::string tail = stem.substr(pos + 3);
    if (tail.empty())
        return false;
    for (char c : tail)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    if (basePart)
        *basePart = stem.substr(0, pos);
    if (step)
        *step = std::stoi(tail);
    return true;
}

// Reads a scalar TimeValue from the first block that carries a FieldData/TimeValue
// dataset.  Returns fallback when none found.
static double ReadFileTimeValue(const std::string& filePath, double fallback)
{
    try
    {
        H5Handle f = OpenFileReadOnly(filePath);
        for (const std::string& child : ListChildNames(f.Get(), "/VTKHDF"))
        {
            if (child == "Assembly")
                continue;
            const std::string tvPath = "/VTKHDF/" + child + "/FieldData/TimeValue";
            if (!PathExists(f.Get(), tvPath))
                continue;
            const DatasetInfo info = InspectDataset(f.Get(), tvPath);
            if (info.typeClass == H5T_FLOAT)
            {
                std::vector<double> v = ReadNumericDataset<double>(f.Get(), tvPath, H5T_NATIVE_DOUBLE);
                if (!v.empty())
                    return v.front();
            }
            else if (info.typeClass == H5T_INTEGER)
            {
                std::vector<long long> v = ReadNumericDataset<long long>(f.Get(), tvPath, H5T_NATIVE_LLONG);
                if (!v.empty())
                    return static_cast<double>(v.front());
            }
        }
    }
    catch (const std::exception&)
    {
    }
    return fallback;
}

static std::vector<TimeSampleInfo> ScanTimeSamples(const std::string& casePath)
{
    std::vector<TimeSampleInfo> samples;
    const fs::path p(casePath);
    const std::string stem = p.stem().string();
    const std::string ext = p.extension().string(); // ".vtkhdf"

    std::string base;
    int caseStep = 0;
    const bool hasPattern = ParseTimestepStem(stem, &base, &caseStep);
    if (!hasPattern)
    {
        // Single-file case.
        TimeSampleInfo s;
        s.filePath = casePath;
        s.stepIndex = 0;
        s.caseTime = ReadFileTimeValue(casePath, 0.0);
        samples.push_back(std::move(s));
        return samples;
    }

    const std::string prefix = base + "_t_";
    for (const auto& entry : fs::directory_iterator(p.parent_path()))
    {
        if (!entry.is_regular_file())
            continue;
        const fs::path childPath = entry.path();
        if (childPath.extension() != ext)
            continue;
        const std::string childStem = childPath.stem().string();
        if (childStem.rfind(prefix, 0) != 0)
            continue;
        std::string childBase;
        int step = 0;
        if (!ParseTimestepStem(childStem, &childBase, &step))
            continue;
        if (childBase != base)
            continue;
        TimeSampleInfo s;
        s.filePath = childPath.string();
        s.stepIndex = step;
        s.caseTime = ReadFileTimeValue(s.filePath, static_cast<double>(step));
        samples.push_back(std::move(s));
    }

    std::sort(samples.begin(), samples.end(),
              [](const TimeSampleInfo& a, const TimeSampleInfo& b) { return a.stepIndex < b.stepIndex; });
    return samples;
}

static std::vector<double> ResolveSampleTimes(const ReadOptions& options, const std::vector<TimeSampleInfo>& samples)
{
    std::vector<double> times;
    times.reserve(samples.size());
    for (const TimeSampleInfo& s : samples)
        times.push_back(ResolveSampleTime(options, s.stepIndex, s.caseTime));
    return times;
}

static double GetSampleTime(const CaseInfo& caseInfo, size_t idx)
{
    return idx < caseInfo.sampleTimes.size() ?
               caseInfo.sampleTimes[idx] :
               ResolveSampleTime(caseInfo.options, caseInfo.samples[idx].stepIndex, caseInfo.samples[idx].caseTime);
}

// -------------------------------------------------------------------------
// Assembly / structure discovery
// -------------------------------------------------------------------------

static std::vector<AssemblyLink> ScanAssemblyCategory(const std::string& filePath, const std::string& category)
{
    std::vector<AssemblyLink> out;
    const std::string parent = "/VTKHDF/Assembly/" + category;
    H5Handle f = OpenFileReadOnly(filePath);
    if (!PathExists(f.Get(), parent))
        return out;
    for (const std::string& name : ListChildNames(f.Get(), parent))
    {
        AssemblyLink link;
        link.name = name;
        link.blockPath = ReadLinkTarget(f.Get(), parent, name);
        if (link.blockPath.empty())
            continue;
        out.push_back(std::move(link));
    }
    return out;
}

// Find a template source file: prefer a sibling "particle_templates" directory
// with a `<prefix>particle_templates_t_0.vtkhdf` if it exists; otherwise use
// the first sample file.
static std::string ResolveTemplateSource(const std::string& casePath, const TimeSampleInfo& firstSample)
{
    const fs::path parent = fs::path(casePath).parent_path();
    const fs::path candidateDir = parent / "particle_templates";
    if (fs::is_directory(candidateDir))
    {
        for (const auto& entry : fs::directory_iterator(candidateDir))
        {
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() == ".vtkhdf")
                return entry.path().string();
        }
    }
    return firstSample.filePath;
}

static std::vector<TemplateInfo> ScanTemplates(const std::string& sourceFile)
{
    std::vector<TemplateInfo> out;
    auto append = [&](const AssemblyLink& link)
    {
        TemplateInfo t;
        t.name = link.name;
        t.blockPath = link.blockPath;
        t.sourceFile = sourceFile;
        out.push_back(std::move(t));
    };

    for (AssemblyLink& link : ScanAssemblyCategory(sourceFile, "ParticleTemplates"))
        append(link);
    if (!out.empty())
        return out;

    // Standalone particle-template files place templates as direct soft-links
    // under /VTKHDF/Assembly rather than in a ParticleTemplates subgroup.
    static const std::array<std::string, 4> kReservedCategories = {
        "Particles", "Contacts", "Bonds", "Geometries"
    };
    H5Handle f = OpenFileReadOnly(sourceFile);
    if (!PathExists(f.Get(), "/VTKHDF/Assembly"))
        return out;
    for (const std::string& name : ListChildNames(f.Get(), "/VTKHDF/Assembly"))
    {
        if (std::find(kReservedCategories.begin(), kReservedCategories.end(), name)
                != kReservedCategories.end())
            continue;
        AssemblyLink link;
        link.name = name;
        link.blockPath = ReadLinkTarget(f.Get(), "/VTKHDF/Assembly", name);
        if (link.blockPath.empty())
            continue;
        append(link);
    }
    return out;
}

static std::vector<GeometryInfo> ScanGeometries(const std::string& firstSampleFile)
{
    std::vector<GeometryInfo> out;
    for (AssemblyLink& link : ScanAssemblyCategory(firstSampleFile, "Geometries"))
    {
        GeometryInfo g;
        g.name = link.name;
        g.blockPath = link.blockPath;
        out.push_back(std::move(g));
    }
    return out;
}

// Categorise a PointData dataset into a supported CAE value type.
static std::optional<FieldInfo> InspectField(const std::string& filePath,
                                             const std::string& datasetPath,
                                             const std::string& sourceName)
{
    H5Handle f = OpenFileReadOnly(filePath);
    const DatasetInfo info = InspectDataset(f.Get(), datasetPath);
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

// Return the first sample that contains PointData for cloudName with non-zero rows.
// Falls back to the last sample if none are non-empty.
static const TimeSampleInfo* FindSampleWithData(const std::vector<TimeSampleInfo>& samples,
                                                const std::string& cloudName)
{
    if (samples.empty())
        return nullptr;

    for (const TimeSampleInfo& s : samples)
    {
        try
        {
            H5Handle f = OpenFileReadOnly(s.filePath);
            const std::string parent = "/VTKHDF/Assembly/Particles";
            if (!PathExists(f.Get(), parent + "/" + cloudName))
                continue;
            const std::string blockPath = ReadLinkTarget(f.Get(), parent, cloudName);
            if (blockPath.empty())
                continue;
            const std::string pdPath = blockPath + "/PointData";
            if (!PathExists(f.Get(), pdPath))
                continue;
            if (ListChildNames(f.Get(), pdPath).empty())
                continue;
            return &s;
        }
        catch (const std::exception&)
        {
        }
    }
    return &samples.back();
}

static std::vector<FieldInfo> ScanParticleFields(const std::string& filePath, const std::string& blockPath)
{
    std::vector<FieldInfo> fields;
    H5Handle f = OpenFileReadOnly(filePath);
    const std::string pd = blockPath + "/PointData";
    if (!PathExists(f.Get(), pd))
        return fields;
    std::vector<std::string> children = ListChildNames(f.Get(), pd);
    std::sort(children.begin(), children.end());
    for (const std::string& name : children)
    {
        const auto info = InspectField(filePath, pd + "/" + name, name);
        if (info)
            fields.push_back(*info);
    }
    return fields;
}

// -------------------------------------------------------------------------
// Case parsing
// -------------------------------------------------------------------------

static CaseInfo ParseCase(const std::string& casePath, const SdfLayer::FileFormatArguments& args)
{
    CaseInfo info;
    info.casePath = casePath;
    info.caseDir = fs::path(casePath).parent_path().string();
    info.options = ParseReadOptions(casePath, args);

    info.samples = ScanTimeSamples(casePath);
    if (info.samples.empty())
        throw cae::FileFormatError("VTKHDF: no readable samples for: " + casePath);
    info.sampleTimes = ResolveSampleTimes(info.options, info.samples);
    info.sampleTimesAreSorted = std::is_sorted(info.sampleTimes.begin(), info.sampleTimes.end());

    TF_DEBUG(CAE_VTKHDF_FILEFORMAT)
        .Msg("[VTKHDF] parsed %zu samples for case='%s'\n", info.samples.size(), casePath.c_str());

    const std::string templateSource = ResolveTemplateSource(casePath, info.samples.front());
    info.templates = ScanTemplates(templateSource);

    // Particle clouds: names come from the ParticleTemplates listing; the
    // per-cloud block path is resolved from the first sample containing data.
    for (const TemplateInfo& t : info.templates)
    {
        ParticleCloudInfo cloud;
        cloud.name = t.name;

        const TimeSampleInfo* discoverySample = FindSampleWithData(info.samples, t.name);
        if (discoverySample)
        {
            H5Handle f = OpenFileReadOnly(discoverySample->filePath);
            const std::string parent = "/VTKHDF/Assembly/Particles";
            if (PathExists(f.Get(), parent + "/" + t.name))
                cloud.blockPath = ReadLinkTarget(f.Get(), parent, t.name);
            if (!cloud.blockPath.empty())
                cloud.fields = ScanParticleFields(discoverySample->filePath, cloud.blockPath);
        }

        if (cloud.blockPath.empty())
        {
            TF_DEBUG(CAE_VTKHDF_FILEFORMAT)
                .Msg("[VTKHDF] no particle block found for template '%s'; skipping cloud\n", t.name.c_str());
            continue;
        }

        info.particleClouds.push_back(std::move(cloud));
    }

    info.geometries = ScanGeometries(info.samples.front().filePath);

    TF_DEBUG(CAE_VTKHDF_FILEFORMAT)
        .Msg("[VTKHDF] templates=%zu clouds=%zu geometries=%zu\n", info.templates.size(), info.particleClouds.size(),
             info.geometries.size());
    return info;
}

// -------------------------------------------------------------------------
// Lazy loaders
// -------------------------------------------------------------------------

// Resolve the per-sample block path for a named particle cloud (may differ
// across samples if block ordering changes).  Returns "" when not present.
static std::string ResolveParticleBlockPath(const std::string& file, const std::string& cloudName)
{
    try
    {
        H5Handle f = OpenFileReadOnly(file);
        const std::string parent = "/VTKHDF/Assembly/Particles";
        if (!PathExists(f.Get(), parent + "/" + cloudName))
            return {};
        return ReadLinkTarget(f.Get(), parent, cloudName);
    }
    catch (const std::exception&)
    {
        return {};
    }
}

static VtValue LoadFieldValue(const std::string& file, const std::string& path, const TfToken& valueType)
{
    if (valueType == TfToken("int[]"))
    {
        VtIntArray v = LoadIntArray(file, path);
        return VtValue::Take(v);
    }
    if (valueType == TfToken("float[]"))
    {
        VtFloatArray v = LoadFloatArray(file, path);
        return VtValue::Take(v);
    }
    if (valueType == TfToken("float3[]"))
    {
        VtVec3fArray v = LoadVec3ArrayF64(file, path);
        return VtValue::Take(v);
    }
    if (valueType == TfToken("float4[]"))
    {
        VtVec4fArray v = LoadVec4Array(file, path);
        return VtValue::Take(v);
    }
    throw cae::FileFormatError("Unsupported VTKHDF field value type");
}

static CaeFileFormatData::Loader MakePositionsLoader(std::string file, std::string cloudName)
{
    return [file = std::move(file), cloudName = std::move(cloudName)]() -> VtValue
    {
        const std::string blockPath = ResolveParticleBlockPath(file, cloudName);
        if (blockPath.empty())
        {
            VtVec3fArray empty;
            return VtValue::Take(empty);
        }
        try
        {
            VtVec3fArray v = LoadPoints(file, blockPath);
            return VtValue::Take(v);
        }
        catch (const std::exception&)
        {
            VtVec3fArray empty;
            return VtValue::Take(empty);
        }
    };
}

static CaeFileFormatData::Loader MakeFieldLoader(std::string file, std::string cloudName, FieldInfo field)
{
    return [file = std::move(file), cloudName = std::move(cloudName), field = std::move(field)]() -> VtValue
    {
        const std::string blockPath = ResolveParticleBlockPath(file, cloudName);
        if (blockPath.empty())
            return VtValue{};
        const std::string path = blockPath + "/PointData/" + field.sourceName;
        try
        {
            return LoadFieldValue(file, path, field.valueType);
        }
        catch (const std::exception&)
        {
            return VtValue{};
        }
    };
}

// -------------------------------------------------------------------------
// USD authoring
// -------------------------------------------------------------------------

static bool ShouldAuthorStructure(const ReadContext& ctx) { return ctx.mode != ReadMode::FileDataOnly; }
static bool ShouldRegisterFileData(const ReadContext& ctx) { return ctx.mode != ReadMode::StructureOnly; }

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

static ReadResult ReadVtkHdf(const std::string& casePath,
                             const SdfLayer::FileFormatArguments& args,
                             ReadMode mode)
{
    const CaseInfo caseInfo = ParseCase(casePath, args);
    ReadContext ctx = CreateReadContext(caseInfo, mode);
    const bool authorStructure = ShouldAuthorStructure(ctx);
    const bool registerFileData = ShouldRegisterFileData(ctx);

    const SdfPath rootPath = caseInfo.options.rootPath;
    if (authorStructure)
        UsdGeomScope::Define(ctx.stage, rootPath);

    const SdfPath particleTypesPath = rootPath.AppendChild(TfToken("ParticleTypes"));
    const SdfPath particlesPath = rootPath.AppendChild(TfToken("Particles"));
    const SdfPath geometriesPath = rootPath.AppendChild(TfToken("GeometryGroups"));
    if (authorStructure)
    {
        UsdGeomScope::Define(ctx.stage, particleTypesPath);
        UsdGeomScope::Define(ctx.stage, particlesPath);
        UsdGeomScope::Define(ctx.stage, geometriesPath);
    }

    // --- Prototype meshes -------------------------------------------------
    if (authorStructure)
    {
        for (const TemplateInfo& t : caseInfo.templates)
        {
            const TfToken primName(TfMakeValidIdentifier(t.name));
            const SdfPath meshPath = particleTypesPath.AppendChild(primName);
            UsdGeomMesh mesh = UsdGeomMesh::Define(ctx.stage, meshPath);

            VtVec3fArray points;
            VtIntArray indices;
            VtIntArray counts;
            try
            {
                LoadPolyDataMesh(t.sourceFile, t.blockPath, &points, &indices, &counts);
            }
            catch (const std::exception& ex)
            {
                TF_DEBUG(CAE_VTKHDF_FILEFORMAT)
                    .Msg("[VTKHDF] template '%s' load failed: %s\n", t.name.c_str(), ex.what());
            }
            mesh.CreatePointsAttr().Set(points);
            mesh.CreateFaceVertexIndicesAttr().Set(indices);
            mesh.CreateFaceVertexCountsAttr().Set(counts);

            OmniSciEdemParticleTypeAPI api = OmniSciEdemParticleTypeAPI::Apply(mesh.GetPrim());
            api.CreateNameAttr().Set(t.name);
            api.CreateSourceNodeAttr().Set(t.blockPath);
            api.CreateShapeKindAttr().Set(TfToken("polyhedral"));
        }
    }

    // --- Geometry (static) meshes ----------------------------------------
    if (authorStructure)
    {
        for (const GeometryInfo& g : caseInfo.geometries)
        {
            const TfToken primName(TfMakeValidIdentifier(g.name));
            const SdfPath meshPath = geometriesPath.AppendChild(primName);
            UsdGeomMesh mesh = UsdGeomMesh::Define(ctx.stage, meshPath);

            VtVec3fArray points;
            VtIntArray indices;
            VtIntArray counts;
            try
            {
                LoadPolyDataMesh(caseInfo.samples.front().filePath, g.blockPath, &points, &indices, &counts);
            }
            catch (const std::exception& ex)
            {
                TF_DEBUG(CAE_VTKHDF_FILEFORMAT)
                    .Msg("[VTKHDF] geometry '%s' load failed: %s\n", g.name.c_str(), ex.what());
            }
            mesh.CreatePointsAttr().Set(points);
            mesh.CreateFaceVertexIndicesAttr().Set(indices);
            mesh.CreateFaceVertexCountsAttr().Set(counts);

            OmniSciEdemGeometryGroupAPI api = OmniSciEdemGeometryGroupAPI::Apply(mesh.GetPrim());
            api.CreateNameAttr().Set(g.name);
            api.CreateSourceNodeAttr().Set(g.blockPath);
        }
    }

    // --- Particle clouds --------------------------------------------------
    for (const ParticleCloudInfo& cloud : caseInfo.particleClouds)
    {
        const TfToken primName(TfMakeValidIdentifier(cloud.name));
        const SdfPath cloudPath = particlesPath.AppendChild(primName);
        UsdPrim cloudPrim;

        if (authorStructure)
        {
            cloudPrim = OmniSciDataset::Define(ctx.stage, cloudPath).GetPrim();
            OmniSciCaePointCloudAPI::Apply(cloudPrim);
            OmniSciEdemParticleCloudAPI cloudAPI = OmniSciEdemParticleCloudAPI::Apply(cloudPrim);
            cloudAPI.CreateNameAttr().Set(cloud.name);
            cloudAPI.CreateSourceNodeAttr().Set(cloud.blockPath);
            cloudAPI.CreatePrototypeRel().SetTargets({ particleTypesPath.AppendChild(primName) });

            OmniSciArrayAPI pointsArray = OmniSciArrayAPI::Apply(cloudPrim, OmniSciCaeTokens->points);
            pointsArray.CreateDeviceAttr().Set(TfToken("cpu"));
        }

        // Positions
        if (registerFileData)
        {
            if (caseInfo.samples.size() > 1)
            {
                std::vector<std::pair<double, CaeFileFormatData::Loader>> pos;
                pos.reserve(caseInfo.samples.size());
                for (size_t i = 0; i < caseInfo.samples.size(); ++i)
                    pos.push_back({ GetSampleTime(caseInfo, i),
                                    MakePositionsLoader(caseInfo.samples[i].filePath, cloud.name) });
                RegisterTimeSamples(ctx, cloudPath, MakeArrayValueAttrName(OmniSciCaeTokens->points),
                                    TfToken("float3[]"), std::move(pos));
            }
            else
            {
                ctx.fileData->RegisterLazySingleState(
                    cloudPath, MakeArrayValueAttrName(OmniSciCaeTokens->points), TfToken("float3[]"),
                    GetSampleTime(caseInfo, 0),
                    MakePositionsLoader(caseInfo.samples.front().filePath, cloud.name));
            }
        }

        // Per-vertex fields
        for (const FieldInfo& field : cloud.fields)
        {
            if (authorStructure)
            {
                OmniSciFieldAPI fieldAPI = OmniSciFieldAPI::Apply(cloudPrim, field.instanceName);
                fieldAPI.CreateNameAttr().Set(field.sourceName);
                fieldAPI.CreateAssociationAttr().Set(TfToken("node"));

                OmniSciArrayAPI arrayAPI = OmniSciArrayAPI::Apply(cloudPrim, field.instanceName);
                arrayAPI.CreateDeviceAttr().Set(TfToken("cpu"));
            }

            if (!registerFileData)
                continue;
            if (caseInfo.samples.size() > 1)
            {
                std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
                samples.reserve(caseInfo.samples.size());
                for (size_t i = 0; i < caseInfo.samples.size(); ++i)
                    samples.push_back({ GetSampleTime(caseInfo, i),
                                        MakeFieldLoader(caseInfo.samples[i].filePath, cloud.name, field) });
                RegisterTimeSamples(ctx, cloudPath, MakeArrayValueAttrName(field.instanceName), field.valueType,
                                    std::move(samples));
            }
            else
            {
                ctx.fileData->RegisterLazySingleState(
                    cloudPath, MakeArrayValueAttrName(field.instanceName), field.valueType,
                    GetSampleTime(caseInfo, 0),
                    MakeFieldLoader(caseInfo.samples.front().filePath, cloud.name, field));
            }
        }
    }

    return { ctx.layer, ctx.fileData };
}

} // namespace detail

// -------------------------------------------------------------------------
// SdfFileFormat overrides
// -------------------------------------------------------------------------

OmniSciVtkHdfFileFormat::OmniSciVtkHdfFileFormat()
    : SdfFileFormat(OmniSciVtkHdfFileFormatTokens->Id,
                    OmniSciVtkHdfFileFormatTokens->Version,
                    OmniSciVtkHdfFileFormatTokens->Target,
                    OmniSciVtkHdfFileFormatTokens->Extension)
{
}

OmniSciVtkHdfFileFormat::~OmniSciVtkHdfFileFormat() = default;

bool OmniSciVtkHdfFileFormat::CanRead(const std::string& filePath) const
{
    if (TfGetExtension(filePath) != OmniSciVtkHdfFileFormatTokens->Extension)
    {
        TF_DEBUG(CAE_VTKHDF_FILEFORMAT)
            .Msg("OmniSciVtkHdfFileFormat::CanRead('%s') -> false (extension mismatch)\n", filePath.c_str());
        return false;
    }
    if (!CaeCanScanAdjacentFiles(filePath, ArResolvedPath(filePath)))
        return false;

    // Cheapest possible content check: file opens as HDF5 and has "/VTKHDF".
    hid_t fileId = -1;
    H5E_BEGIN_TRY { fileId = H5Fopen(filePath.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT); } H5E_END_TRY;
    if (fileId < 0)
        return false;
    detail::H5Handle f(fileId);
    const bool hasRoot = detail::PathExists(f.Get(), "/VTKHDF");
    TF_DEBUG(CAE_VTKHDF_FILEFORMAT)
        .Msg("OmniSciVtkHdfFileFormat::CanRead('%s') -> %d\n", filePath.c_str(), hasRoot ? 1 : 0);
    return hasRoot;
}

bool OmniSciVtkHdfFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool /*metadataOnly*/) const
{
    TF_DEBUG(CAE_VTKHDF_FILEFORMAT).Msg("OmniSciVtkHdfFileFormat::Read('%s')\n", resolvedPath.c_str());

    if (!TF_VERIFY(layer))
        return false;

    const auto& fmtArgs = layer->GetFileFormatArguments();
    const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
    try
    {
        CaeRequireAdjacentFileScanning("VTKHDF", identifier, ArResolvedPath(resolvedPath));
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciVtkHdfFileFormat: %s", ex.what());
        return false;
    }

    // Strip the "_t_<N>" suffix from the default mount path so all frames of
    // a series compose onto the same root prim.
    const SdfLayer::FileFormatArguments patchedFmtArgs = detail::PatchMountPath(identifier, fmtArgs);
    const auto readArgs = CaePrepareResolverArguments(identifier, patchedFmtArgs);
    try
    {
        auto result = detail::ReadVtkHdf(resolvedPath, readArgs, detail::ReadMode::StructureAndFileData);
        if (!result.first || !result.second)
            return false;

        result.second->CopyFrom(_GetLayerData(*result.first));
        SdfAbstractDataRefPtr fileData = result.second;
        _SetLayerData(layer, fileData);

        SdfPath rootPath = CaeResolveRootPrimPath(identifier, patchedFmtArgs);
        CaeAuthorMountPathOvers(layer, rootPath);
        return true;
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciVtkHdfFileFormat: %s", ex.what());
        return false;
    }
}

void OmniSciVtkHdfFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                  const PcpDynamicFileFormatContext& context,
                                                                  FileFormatArguments* args,
                                                                  VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciVtkHdfFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, detail::GetDynamicFileFormatArgs());
}

bool OmniSciVtkHdfFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciVtkHdfFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciVtkHdfFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciVtkHdfFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
