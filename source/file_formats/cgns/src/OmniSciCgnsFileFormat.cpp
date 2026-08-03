// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciCgnsFileFormat.h"

#include "CGNSMutex.h"
#include "CaeFileFormatData.h"
#include "ContainerUtils.h"
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
#include <omniSciCgns/flowSolutionAPI.h>
#include <omniSciCgns/gridCoordinatesAPI.h>
#include <omniSciCgns/tokens.h>
#include <omniSciCgns/unstructuredElementsAPI.h>
#include <omniSciCgns/zoneAPI.h>
#include <omniSciFileFormatArgs/tokens.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/pcp/dynamicFileFormatContext.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/tokens.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <cgns_io.h>
#include <cgnslib.h>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <type_traits>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciCgnsFileFormatTokens, OMNI_SCI_CGNS_FILE_FORMAT_TOKENS);

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace detail
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 8> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciCgnsFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeScale, OmniSciCgnsFileFormatTokens->ArgTimeScale },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeOffset, OmniSciCgnsFileFormatTokens->ArgTimeOffset },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeSource, OmniSciCgnsFileFormatTokens->ArgTimeSource },
            { OmniSciFileFormatArgsTokens->omniCaeFormatCgnsBaseName, OmniSciCgnsFileFormatTokens->ArgBaseName },
            { OmniSciFileFormatArgsTokens->omniCaeFormatCgnsZoneName, OmniSciCgnsFileFormatTokens->ArgZoneName },
            { OmniSciFileFormatArgsTokens->omniCaeFormatCgnsIntSize, OmniSciCgnsFileFormatTokens->ArgIntSize },
            { OmniSciFileFormatArgsTokens->omniCaeFormatCgnsFloatSize, OmniSciCgnsFileFormatTokens->ArgFloatSize },
        } };

    return DynamicFileFormatArgs;
}

void call_safe(int ierr, const char* details)
{
    if (ierr != CG_OK)
    {
        TF_ERROR(CAE_CGNS_FILEFORMAT, "CGNS error: %s (%d) in %s", cg_get_error(), ierr, details);
        throw cae::FileFormatError(details);
    }
}

template <typename T>
SdfPath MakeChildPath(const T& parentPrim, const std::string& name)
{
    return parentPrim.GetPath().AppendChild(TfToken(TfMakeValidIdentifier(name)));
}

TfToken GetElementType(CGNS_ENUMT(ElementType_t) t)
{
#define DO_CASE(x)                                                                                                     \
    case CGNS_ENUMV(x):                                                                                                \
        return TfToken(#x);

    switch (t)
    {
        DO_CASE(ElementTypeNull);
        DO_CASE(ElementTypeUserDefined);
        DO_CASE(NODE);
        DO_CASE(BAR_2);
        DO_CASE(BAR_3);
        DO_CASE(TRI_3);
        DO_CASE(TRI_6);
        DO_CASE(QUAD_4);
        DO_CASE(QUAD_8);
        DO_CASE(QUAD_9);
        DO_CASE(TETRA_4);
        DO_CASE(TETRA_10);
        DO_CASE(PYRA_5);
        DO_CASE(PYRA_14);
        DO_CASE(PENTA_6);
        DO_CASE(PENTA_15);
        DO_CASE(PENTA_18);
        DO_CASE(HEXA_8);
        DO_CASE(HEXA_20);
        DO_CASE(HEXA_27);
        DO_CASE(MIXED);
        DO_CASE(PYRA_13);
        DO_CASE(NGON_n);
        DO_CASE(NFACE_n);
        DO_CASE(BAR_4);
        DO_CASE(TRI_9);
        DO_CASE(TRI_10);
        DO_CASE(QUAD_12);
        DO_CASE(QUAD_16);
        DO_CASE(TETRA_16);
        DO_CASE(TETRA_20);
        DO_CASE(PYRA_21);
        DO_CASE(PYRA_29);
        DO_CASE(PYRA_30);
        DO_CASE(PENTA_24);
        DO_CASE(PENTA_38);
        DO_CASE(PENTA_40);
        DO_CASE(PENTA_75);
        DO_CASE(HEXA_32);
        DO_CASE(HEXA_56);
        DO_CASE(HEXA_64);
        DO_CASE(HEXA_44);
        DO_CASE(HEXA_98);
        DO_CASE(HEXA_125);
    default:
        break;
    }
    return TfToken("ElementTypeNull");
}

using ReadCGNSResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;

// ---------------------------------------------------------------------------
// CGNS-specific helpers for building lazy loaders
// ---------------------------------------------------------------------------

static CGNS_ENUMT(DataType_t) ResolveDataType(CGNS_ENUMT(DataType_t) fileType, int intSize, int floatSize)
{
    if (fileType == CGNS_ENUMV(RealSingle) || fileType == CGNS_ENUMV(RealDouble))
    {
        if (floatSize == 32)
            return CGNS_ENUMV(RealSingle);
        if (floatSize == 64)
            return CGNS_ENUMV(RealDouble);
        return fileType;
    }
    if (intSize == 32)
        return CGNS_ENUMV(Integer);
    if (intSize == 64)
        return CGNS_ENUMV(LongInteger);
    return fileType;
}

static TfToken DataTypeToSdfTypeName(CGNS_ENUMT(DataType_t) dt)
{
    if (dt == CGNS_ENUMV(RealSingle))
        return TfToken("float[]");
    if (dt == CGNS_ENUMV(RealDouble))
        return TfToken("double[]");
    if (dt == CGNS_ENUMV(Integer))
        return TfToken("int[]");
    return TfToken("int64[]"); // LongInteger / default
}

struct ReadOptions
{
    double timeScale = 1.0;
    double timeOffset = 0.0;
    int intSize = 0;
    int floatSize = 0;
    SdfPath rootPath;
    CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All;
    std::string timeSource = "TimeStep";
    std::string filterBaseName;
    std::string filterZoneName;
};

struct ReadContext
{
    ReadContext() = default;
    ReadContext(const ReadContext&) = delete;
    ReadContext& operator=(const ReadContext&) = delete;
    ReadContext(ReadContext&&) noexcept = default;
    ReadContext& operator=(ReadContext&&) noexcept = default;

    int cgFile = -1;
    std::string fileName;
    ReadOptions options;
    SdfLayerRefPtr layer;
    UsdStageRefPtr stage;
    CaeFileFormatDataRefPtr fileData;
};

struct BaseTimeInfo
{
    int numSteps = 0;
    std::vector<double> sampleValues;
    std::vector<double> resolvedSampleTimes;
};

struct ZoneIterativeInfo
{
    cae::StringMap<std::vector<std::string>> flowSolutionPointers;
    cae::StringSet allFlowSolutionPointers;
};

static ReadOptions ParseReadOptions(const std::string& fname, const SdfLayer::FileFormatArguments& args)
{
    auto getArg = [&](const TfToken& key)
    {
        auto it = args.find(key.GetString());
        return it != args.end() ? it->second : std::string{};
    };
    auto parseIntArg = [&](const TfToken& key, int validA, int validB) -> int
    {
        const std::string s = getArg(key);
        if (s.empty())
            return 0;
        const int v = std::stoi(s);
        return (v == validA || v == validB) ? v : 0;
    };

    ReadOptions options;
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);
    const std::string timeScale = getArg(OmniSciCgnsFileFormatTokens->ArgTimeScale);
    const std::string timeOffset = getArg(OmniSciCgnsFileFormatTokens->ArgTimeOffset);

    options.timeScale = timeScale.empty() ? 1.0 : std::stod(timeScale);
    options.timeOffset = timeOffset.empty() ? 0.0 : std::stod(timeOffset);
    options.intSize = parseIntArg(OmniSciCgnsFileFormatTokens->ArgIntSize, 32, 64);
    options.floatSize = parseIntArg(OmniSciCgnsFileFormatTokens->ArgFloatSize, 32, 64);
    options.timeSource = getArg(OmniSciCgnsFileFormatTokens->ArgTimeSource);
    if (options.timeSource.empty())
        options.timeSource = "TimeStep";
    options.filterBaseName = getArg(OmniSciCgnsFileFormatTokens->ArgBaseName);
    options.filterZoneName = getArg(OmniSciCgnsFileFormatTokens->ArgZoneName);

    options.rootPath = CaeResolveRootPrimPath(fname, args);

    return options;
}

static SdfPath MakeChildPath(const SdfPath& parentPath, const std::string& name)
{
    return parentPath.AppendChild(TfToken(TfMakeValidIdentifier(name)));
}

template <typename Fn>
CaeFileFormatData::Loader MakeCGNSLoader(const std::string& fname, const std::string& varDesc, Fn fn)
{
    return [path = fname, desc = varDesc, fn = std::move(fn)]() -> VtValue
    {
        TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("CaeFileFormatData: loading '%s' from '%s'\n", desc.c_str(), path.c_str());
        std::scoped_lock lock(GetCGNSMutex());
        int cgf = -1;
        if (cg_open(path.c_str(), CG_MODE_READ, &cgf) != CG_OK)
        {
            TF_RUNTIME_ERROR("CaeFileFormatData: failed to reopen '%s'.", path.c_str());
            return VtValue();
        }
        VtValue v = fn(cgf);
        cg_close(cgf);
        return v;
    };
}

static TfToken MakeArrayValueAttrName(const TfToken& arrayName)
{
    return TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

static TfToken GetGridCoordinateInstanceName(const std::string& coordName, int coordIndex)
{
    if (coordName == "CoordinateX")
        return OmniSciCgnsTokens->gridCoordinatesX;
    if (coordName == "CoordinateY")
        return OmniSciCgnsTokens->gridCoordinatesY;
    if (coordName == "CoordinateZ")
        return OmniSciCgnsTokens->gridCoordinatesZ;

    switch (coordIndex)
    {
    case 1:
        return OmniSciCgnsTokens->gridCoordinatesX;
    case 2:
        return OmniSciCgnsTokens->gridCoordinatesY;
    case 3:
        return OmniSciCgnsTokens->gridCoordinatesZ;
    default:
        return TfToken();
    }
}

static size_t GetArrayValueCount(int rank, const cgsize_t* dims)
{
    if (rank <= 0)
        return 0;

    size_t count = 1;
    for (int dim = 0; dim < rank; ++dim)
        count *= static_cast<size_t>(dims[dim]);
    return count;
}

static VtValue ReadCurrentCGNSArrayAsValue(int arrayIndex, CGNS_ENUMT(DataType_t) effType, size_t valueCount)
{
    if (effType == CGNS_ENUMV(RealSingle))
    {
        UninitializedVtArray<float> buf = MakeUninitializedVtArray<float>(valueCount);
        call_safe(cg_array_read_as(arrayIndex, CGNS_ENUMV(RealSingle), buf.data), "read data array");
        return VtValue::Take(buf.array);
    }
    if (effType == CGNS_ENUMV(RealDouble))
    {
        UninitializedVtArray<double> buf = MakeUninitializedVtArray<double>(valueCount);
        call_safe(cg_array_read_as(arrayIndex, CGNS_ENUMV(RealDouble), buf.data), "read data array");
        return VtValue::Take(buf.array);
    }
    if (effType == CGNS_ENUMV(Integer))
    {
        UninitializedVtArray<int> buf = MakeUninitializedVtArray<int>(valueCount);
        call_safe(cg_array_read_as(arrayIndex, CGNS_ENUMV(Integer), buf.data), "read data array");
        return VtValue::Take(buf.array);
    }

    UninitializedVtArray<int64_t> buf = MakeUninitializedVtArray<int64_t>(valueCount);
    call_safe(cg_array_read_as(arrayIndex, CGNS_ENUMV(LongInteger), buf.data), "read data array");
    return VtValue::Take(buf.array);
}

static const char* GetIterativeArrayNameForTimeSource(std::string_view timeSource)
{
    if (timeSource == "TimeValue")
        return "TimeValues";
    if (timeSource == "IterationValue")
        return "IterationValues";
    return nullptr;
}

static VtValue ConvertCgsizeBufferToVt(const cgsize_t* tmp, size_t count, CGNS_ENUMT(DataType_t) effIntType)
{
    if (effIntType == CGNS_ENUMV(Integer))
    {
        UninitializedVtArray<int> buf = MakeUninitializedVtArray<int>(count);
        for (size_t i = 0; i < count; ++i)
            buf.data[i] = static_cast<int>(tmp[i]);
        return VtValue::Take(buf.array);
    }
    UninitializedVtArray<int64_t> buf = MakeUninitializedVtArray<int64_t>(count);
    for (size_t i = 0; i < count; ++i)
        buf.data[i] = static_cast<int64_t>(tmp[i]);
    return VtValue::Take(buf.array);
}

static VtValue ReadElementConnectivityAsValue(
    int cgf, int base, int zone, int section, cgsize_t valueCount, CGNS_ENUMT(DataType_t) effIntType)
{
    const auto count = static_cast<size_t>(valueCount);
    if (effIntType == CGNS_ENUMV(Integer))
    {
        if constexpr (std::is_same_v<cgsize_t, int>)
        {
            UninitializedVtArray<int> buf = MakeUninitializedVtArray<int>(count);
            call_safe(cg_elements_read(cgf, base, zone, section, reinterpret_cast<cgsize_t*>(buf.data), nullptr),
                      "read element connectivity");
            return VtValue::Take(buf.array);
        }
        else
        {
            UninitializedVtArray<cgsize_t> tmp = MakeUninitializedVtArray<cgsize_t>(count);
            call_safe(cg_elements_read(cgf, base, zone, section, tmp.data, nullptr), "read element connectivity");
            return ConvertCgsizeBufferToVt(tmp.data, count, effIntType);
        }
    }

    if constexpr (std::is_same_v<cgsize_t, int64_t>)
    {
        UninitializedVtArray<int64_t> buf = MakeUninitializedVtArray<int64_t>(count);
        call_safe(cg_elements_read(cgf, base, zone, section, reinterpret_cast<cgsize_t*>(buf.data), nullptr),
                  "read element connectivity");
        return VtValue::Take(buf.array);
    }
    else
    {
        UninitializedVtArray<cgsize_t> tmp = MakeUninitializedVtArray<cgsize_t>(count);
        call_safe(cg_elements_read(cgf, base, zone, section, tmp.data, nullptr), "read element connectivity");
        return ConvertCgsizeBufferToVt(tmp.data, count, effIntType);
    }
}

struct PolyElementValues
{
    VtValue connectivity;
    VtValue offsets;
};

static PolyElementValues ReadPolyElementsAsValues(int cgf,
                                                  int base,
                                                  int zone,
                                                  int section,
                                                  cgsize_t connectivityCount,
                                                  cgsize_t offsetCount,
                                                  CGNS_ENUMT(DataType_t) effIntType)
{
    if (effIntType == CGNS_ENUMV(Integer))
    {
        if constexpr (std::is_same_v<cgsize_t, int>)
        {
            UninitializedVtArray<int> conn = MakeUninitializedVtArray<int>(static_cast<size_t>(connectivityCount));
            UninitializedVtArray<int> off = MakeUninitializedVtArray<int>(static_cast<size_t>(offsetCount));
            call_safe(cg_poly_elements_read(cgf, base, zone, section, reinterpret_cast<cgsize_t*>(conn.data),
                                            reinterpret_cast<cgsize_t*>(off.data), nullptr),
                      "read poly elements");
            return { VtValue::Take(conn.array), VtValue::Take(off.array) };
        }
        else
        {
            const auto connCount = static_cast<size_t>(connectivityCount);
            const auto offCount = static_cast<size_t>(offsetCount);
            UninitializedVtArray<cgsize_t> conn = MakeUninitializedVtArray<cgsize_t>(connCount);
            UninitializedVtArray<cgsize_t> off = MakeUninitializedVtArray<cgsize_t>(offCount);
            call_safe(
                cg_poly_elements_read(cgf, base, zone, section, conn.data, off.data, nullptr), "read poly elements");
            return { ConvertCgsizeBufferToVt(conn.data, connCount, effIntType),
                     ConvertCgsizeBufferToVt(off.data, offCount, effIntType) };
        }
    }

    if constexpr (std::is_same_v<cgsize_t, int64_t>)
    {
        UninitializedVtArray<int64_t> conn = MakeUninitializedVtArray<int64_t>(static_cast<size_t>(connectivityCount));
        UninitializedVtArray<int64_t> off = MakeUninitializedVtArray<int64_t>(static_cast<size_t>(offsetCount));
        call_safe(cg_poly_elements_read(cgf, base, zone, section, reinterpret_cast<cgsize_t*>(conn.data),
                                        reinterpret_cast<cgsize_t*>(off.data), nullptr),
                  "read poly elements");
        return { VtValue::Take(conn.array), VtValue::Take(off.array) };
    }
    else
    {
        const auto connCount = static_cast<size_t>(connectivityCount);
        const auto offCount = static_cast<size_t>(offsetCount);
        UninitializedVtArray<cgsize_t> conn = MakeUninitializedVtArray<cgsize_t>(connCount);
        UninitializedVtArray<cgsize_t> off = MakeUninitializedVtArray<cgsize_t>(offCount);
        call_safe(cg_poly_elements_read(cgf, base, zone, section, conn.data, off.data, nullptr), "read poly elements");
        return { ConvertCgsizeBufferToVt(conn.data, connCount, effIntType),
                 ConvertCgsizeBufferToVt(off.data, offCount, effIntType) };
    }
}

struct PolyElementCache
{
    std::once_flag once;
    PolyElementValues values;
};

static ReadContext CreateReadContext(int cgFile, const std::string& fname, const ReadOptions& options)
{
    ReadContext ctx;
    ctx.cgFile = cgFile;
    ctx.fileName = fname;
    ctx.options = options;
    ctx.layer = SdfLayer::CreateAnonymous();
    ctx.stage = UsdStage::Open(ctx.layer);
    UsdGeomSetStageUpAxis(ctx.stage, UsdGeomTokens->z);
    // Canonical simulation-seconds timecodes: self-describe the unit so
    // direct Stage.Open plays at real-time and composition tooling can
    // reconcile host-stage TCPS through SdfLayerOffset.
    ctx.stage->SetTimeCodesPerSecond(1.0);
    ctx.fileData = CreateCaeFileFormatData(ctx.options.cacheMode);
    return ctx;
}

static void ResolveBaseSampleTimes(const ReadContext& ctx, BaseTimeInfo& info)
{
    const size_t count =
        info.sampleValues.empty() ? static_cast<size_t>(std::max(info.numSteps, 0)) : info.sampleValues.size();
    info.resolvedSampleTimes.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const double rawTime = info.sampleValues.empty() ? static_cast<double>(i) : info.sampleValues[i];
        info.resolvedSampleTimes.push_back(ctx.options.timeOffset + ctx.options.timeScale * rawTime);
    }
}

static BaseTimeInfo ReadBaseTimeInfo(const ReadContext& ctx, int base)
{
    BaseTimeInfo info;
    std::array<char, 1024> nodeName = {};
    const int biterErr = cg_biter_read(ctx.cgFile, base, nodeName.data(), &info.numSteps);
    TF_DEBUG(CAE_CGNS_FILEFORMAT)
        .Msg("  [biter] cg_biter_read base=%d -> err=%d nsteps=%d nodeName='%s'\n", base, biterErr, info.numSteps,
             nodeName.data());

    if (biterErr != CG_OK || info.numSteps <= 0)
        return info;

    const char* arrayName = GetIterativeArrayNameForTimeSource(ctx.options.timeSource);
    if (!arrayName)
    {
        ResolveBaseSampleTimes(ctx, info);
        return info;
    }

    const int gotoErr = cg_goto(ctx.cgFile, base, "BaseIterativeData_t", 1, NULL);
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("  [biter] cg_goto BaseIterativeData_t -> err=%d\n", gotoErr);
    if (gotoErr != CG_OK)
    {
        TF_WARN("OmniSciCgnsFileFormat: cg_goto BaseIterativeData_t failed (err=%d): %s", gotoErr, cg_get_error());
        info.numSteps = 0;
        ResolveBaseSampleTimes(ctx, info);
        return info;
    }

    int narrays = 0;
    cg_narrays(&narrays);
    TF_DEBUG(CAE_CGNS_FILEFORMAT)
        .Msg("  [biter] narrays=%d using '%s' for timeSource='%s'\n", narrays, arrayName, ctx.options.timeSource.c_str());

    std::array<char, 1024> name = {};
    for (int na = 1; na <= narrays; ++na)
    {
        CGNS_ENUMT(DataType_t) dtype;
        std::array<cgsize_t, 12> dims = {};
        int rank = 0;
        cg_array_info(na, name.data(), &dtype, &rank, dims.data());
        TF_DEBUG(CAE_CGNS_FILEFORMAT)
            .Msg( // NOSONAR: this is the documented TF_DEBUG macro syntax.
                "  [biter] array[%d] name='%s' rank=%d dims[0]=%lld\n", na, name.data(), rank, (long long)dims[0]);

        if (strcmp(name.data(), arrayName) != 0)
            continue;

        info.sampleValues.resize(dims[0]);
        const int readErr = cg_array_read_as(na, CGNS_ENUMV(RealDouble), info.sampleValues.data());
        if (readErr != CG_OK)
            info.sampleValues.clear();
        break;
    }

    ResolveBaseSampleTimes(ctx, info);
    return info;
}

static double GetSingleStateSampleTime(const ReadContext& ctx, const BaseTimeInfo& baseTimeInfo)
{
    return baseTimeInfo.resolvedSampleTimes.empty() ? ctx.options.timeOffset : baseTimeInfo.resolvedSampleTimes.front();
}

static SdfPathVector ReadGridCoordinates(
    const ReadContext& ctx, int base, int zone, cgsize_t numNodes, const SdfPath& zonePath, double sampleTime)
{
    int ncoordsNodes = 0;
    call_safe(cg_ngrids(ctx.cgFile, base, zone, &ncoordsNodes), "read num grids");
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("    [zone %d] ngrids=%d\n", zone, ncoordsNodes);

    SdfPathVector gridCoordinatePaths;
    std::array<char, 1024> name = {};
    for (int gc = 1; gc <= ncoordsNodes; ++gc)
    {
        std::array<char, 1024> gcName = {};
        call_safe(cg_grid_read(ctx.cgFile, base, zone, gc, gcName.data()), "read grid name");
        const std::string gcNameStr(gcName.data());
        const SdfPath gridCoordinatesPath = MakeChildPath(zonePath, gcNameStr);
        gridCoordinatePaths.push_back(gridCoordinatesPath);

        const UsdPrim gridCoordinatesPrim =
            ctx.stage->DefinePrim(gridCoordinatesPath, OmniSciCgnsFileFormatTokens->CGNSGridCoordinates);
        OmniSciCgnsGridCoordinatesAPI::Apply(gridCoordinatesPrim);

        call_safe(cg_goto(ctx.cgFile, base, "Zone_t", zone, "GridCoordinates_t", gc, NULL), "goto grid coordinates");

        int ncoords = 0;
        call_safe(cg_narrays(&ncoords), "read ncoords");

        for (int coord = 1; coord <= ncoords; ++coord)
        {
            CGNS_ENUMT(DataType_t) datatype;
            std::array<cgsize_t, 12> dims = {};
            int rank = 0;
            call_safe(cg_array_info(coord, name.data(), &datatype, &rank, dims.data()), "read coord info");

            const std::string coordNameStr(name.data());
            const TfToken coordInstanceName = GetGridCoordinateInstanceName(coordNameStr, coord);
            if (coordInstanceName.IsEmpty())
            {
                TF_DEBUG(CAE_CGNS_FILEFORMAT)
                    .Msg("    [grid %d] skipping unsupported coordinate array '%s'\n", gc, coordNameStr.c_str());
                continue;
            }

            auto arrAPI = OmniSciArrayAPI::Apply(gridCoordinatesPrim, coordInstanceName);
            arrAPI.CreateDeviceAttr().Set(TfToken("cpu"));

            const auto effType = ResolveDataType(datatype, ctx.options.intSize, ctx.options.floatSize);
            const TfToken sdfType = DataTypeToSdfTypeName(effType);
            const size_t valueCount = GetArrayValueCount(rank, dims.data());
            const size_t coordValueCount = valueCount == 0 ? static_cast<size_t>(numNodes) : valueCount;

            auto coordLoader = [base, zone, gc, coord, effType, coordValueCount](int cgf) -> VtValue
            {
                call_safe(cg_goto(cgf, base, "Zone_t", zone, "GridCoordinates_t", gc, NULL), "goto grid coordinates");
                return ReadCurrentCGNSArrayAsValue(coord, effType, coordValueCount);
            };

            ctx.fileData->RegisterLazySingleState(
                gridCoordinatesPath, MakeArrayValueAttrName(coordInstanceName), sdfType, sampleTime,
                MakeCGNSLoader(ctx.fileName, gcNameStr + "/" + coordNameStr, coordLoader));
        }
    }

    return gridCoordinatePaths;
}

static SdfPathVector ReadSections(
    const ReadContext& ctx, int base, int zone, const SdfPath& zonePath, const UsdPrim& zonePrim, double sampleTime)
{
    int nsections = 0;
    call_safe(cg_nsections(ctx.cgFile, base, zone, &nsections), "read num sections");
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("    [zone %d] nsections=%d\n", zone, nsections);

    SdfPathVector sectionPaths;
    std::array<char, 1024> name = {};
    for (int section = 1; section <= nsections; ++section)
    {
        CGNS_ENUMT(ElementType_t) elementType;
        cgsize_t start;
        cgsize_t end;
        int nbndry;
        int parentFlag;
        call_safe(cg_section_read(
                      ctx.cgFile, base, zone, section, name.data(), &elementType, &start, &end, &nbndry, &parentFlag),
                  "read section info");

        const bool isPoly = (elementType == CGNS_ENUMV(NGON_n) || elementType == CGNS_ENUMV(NFACE_n));
        const SdfPath sectionPath = MakeChildPath(zonePath, name.data());

        TF_DEBUG(CAE_CGNS_FILEFORMAT)
            .Msg( // NOSONAR: this is the documented TF_DEBUG macro syntax.
                "    [section %d] name='%s' type=%s range=[%lld,%lld] poly=%d\n", section, name.data(),
                GetElementType(elementType).GetText(), (long long)start, (long long)end, (int)isPoly);

        auto sectionDataset = OmniSciDataset::Define(ctx.stage, sectionPath);
        auto sectionPrim = sectionDataset.GetPrim();

        auto elemAPI = OmniSciCgnsUnstructuredElementsAPI::Apply(sectionPrim);
        elemAPI.CreateElementTypeAttr().Set(GetElementType(elementType));
        elemAPI.CreateElementSizeBoundaryAttr().Set(0);
        elemAPI.CreateElementRangeAttr().Set(GfVec2i(static_cast<int>(start), static_cast<int>(end)));
        elemAPI.CreateZoneRel().SetTargets({ zonePrim.GetPath() });

        auto connAPI = OmniSciArrayAPI::Apply(sectionPrim, OmniSciCgnsTokens->elementConnectivity);
        connAPI.CreateDeviceAttr().Set(TfToken("cpu"));
        if (isPoly)
        {
            auto offsetAPI = OmniSciArrayAPI::Apply(sectionPrim, OmniSciCgnsTokens->elementStartOffset);
            offsetAPI.CreateDeviceAttr().Set(TfToken("cpu"));
        }

        const auto effIntType = ResolveDataType(CGNS_ENUMV(LongInteger), ctx.options.intSize, ctx.options.floatSize);
        const TfToken sdfConnType = DataTypeToSdfTypeName(effIntType);

        if (isPoly)
        {
            const cgsize_t offsetCount = end - start + 2;
            auto polyCache = std::make_shared<PolyElementCache>();
            auto polyConnLoader = [base, zone, section, polyCache, offsetCount, effIntType](int cgf) -> VtValue
            {
                cgsize_t sz = 0;
                call_safe(cg_ElementDataSize(cgf, base, zone, section, &sz), "read element data size");
                std::call_once(polyCache->once,
                               [cgf, base, zone, section, sz, offsetCount, effIntType, polyCache]() {
                                   polyCache->values =
                                       ReadPolyElementsAsValues(cgf, base, zone, section, sz, offsetCount, effIntType);
                               });
                return polyCache->values.connectivity;
            };
            auto polyOffsetLoader = [base, zone, section, polyCache, offsetCount, effIntType](int cgf) -> VtValue
            {
                cgsize_t sz = 0;
                call_safe(cg_ElementDataSize(cgf, base, zone, section, &sz), "read element data size");
                std::call_once(polyCache->once,
                               [cgf, base, zone, section, sz, offsetCount, effIntType, polyCache]() {
                                   polyCache->values =
                                       ReadPolyElementsAsValues(cgf, base, zone, section, sz, offsetCount, effIntType);
                               });
                return polyCache->values.offsets;
            };
            ctx.fileData->RegisterLazySingleState(
                sectionPath, MakeArrayValueAttrName(OmniSciCgnsTokens->elementConnectivity), sdfConnType, sampleTime,
                MakeCGNSLoader(ctx.fileName, std::string(name.data()) + "/elementConnectivity", polyConnLoader));
            ctx.fileData->RegisterLazySingleState(
                sectionPath, MakeArrayValueAttrName(OmniSciCgnsTokens->elementStartOffset), sdfConnType, sampleTime,
                MakeCGNSLoader(ctx.fileName, std::string(name.data()) + "/elementStartOffset", polyOffsetLoader));
        }
        else
        {
            auto connLoader = [base, zone, section, effIntType](int cgf) -> VtValue
            {
                cgsize_t sz = 0;
                call_safe(cg_ElementDataSize(cgf, base, zone, section, &sz), "read element data size");
                return ReadElementConnectivityAsValue(cgf, base, zone, section, sz, effIntType);
            };
            ctx.fileData->RegisterLazySingleState(
                sectionPath, MakeArrayValueAttrName(OmniSciCgnsTokens->elementConnectivity), sdfConnType, sampleTime,
                MakeCGNSLoader(ctx.fileName, std::string(name.data()) + "/elementConnectivity", connLoader));
        }

        sectionPaths.push_back(sectionPath);
    }

    return sectionPaths;
}

static ZoneIterativeInfo ReadZoneIterativeInfo(const ReadContext& ctx, int base, int zone, int numSteps)
{
    ZoneIterativeInfo info;
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("  [ziter] zone=%d nsteps=%d\n", zone, numSteps);
    if (numSteps <= 0)
        return info;

    std::array<char, 1024> name = {};
    for (int zid = 1;; ++zid)
    {
        const int gotoErr = cg_goto(ctx.cgFile, base, "Zone_t", zone, "ZoneIterativeData_t", zid, NULL);
        TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("  [ziter] cg_goto ZoneIterativeData_t[%d] -> err=%d\n", zid, gotoErr);
        if (gotoErr != CG_OK)
            break;

        int narrays = 0;
        cg_narrays(&narrays);
        for (int na = 1; na <= narrays; ++na)
        {
            CGNS_ENUMT(DataType_t) dtype;
            std::array<cgsize_t, 12> dims = {};
            int rank = 0;
            cg_array_info(na, name.data(), &dtype, &rank, dims.data());
            if (!(TfStringStartsWith(name.data(), "FlowSolution") && TfStringEndsWith(name.data(), "Pointers")))
                continue;

            std::vector<char> buffer(dims[0] * dims[1]);
            call_safe(cg_array_read_as(na, CGNS_ENUMV(Character), buffer.data()), "read flow solution pointers");

            std::vector<std::string> fsps;
            for (int d = 0; d < dims[1]; ++d)
            {
                std::vector<char> temp;
                std::copy_n(std::next(buffer.begin(), d * dims[0]), dims[0], std::back_inserter(temp));
                temp.push_back(0);
                fsps.push_back(TfStringTrim(temp.data()));
            }

            if (!fsps.empty())
            {
                std::copy(fsps.begin(), fsps.end(),
                          std::inserter(info.allFlowSolutionPointers, info.allFlowSolutionPointers.end()));
                info.flowSolutionPointers[fsps.front()] = fsps;
            }
        }
    }

    return info;
}

static cae::StringMap<int> BuildSolutionIndexMap(int cgFile, int base, int zone, int numSolutions)
{
    cae::StringMap<int> solNameToIndex;
    for (int s = 1; s <= numSolutions; ++s)
    {
        CGNS_ENUMT(GridLocation_t) location;
        std::array<char, 1024> name = {};
        cg_sol_info(cgFile, base, zone, s, name.data(), &location);
        solNameToIndex[TfStringTrim(std::string(name.data()))] = s;
    }
    return solNameToIndex;
}

static double ResolveSampleTime(const ReadContext& ctx, const BaseTimeInfo& baseTimeInfo, size_t sampleIndex)
{
    if (sampleIndex < baseTimeInfo.resolvedSampleTimes.size())
        return baseTimeInfo.resolvedSampleTimes[sampleIndex];

    const double rawTime = (sampleIndex < baseTimeInfo.sampleValues.size()) ? baseTimeInfo.sampleValues[sampleIndex] :
                                                                              static_cast<double>(sampleIndex);
    return ctx.options.timeOffset + ctx.options.timeScale * rawTime;
}

static void RegisterTimeSamples(const ReadContext& ctx,
                                const SdfPath& primPath,
                                const TfToken& attrName,
                                const TfToken& typeName,
                                std::vector<std::pair<double, CaeFileFormatData::Loader>> samples)
{
    if (samples.size() == 1)
    {
        auto sample = std::move(samples.front());
        ctx.fileData->RegisterLazySingleState(primPath, attrName, typeName, sample.first, std::move(sample.second));
        return;
    }

    const bool sorted = std::is_sorted(
        samples.begin(), samples.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    if (sorted)
        ctx.fileData->RegisterLazyTimeSamplesSorted(primPath, attrName, typeName, std::move(samples));
    else
        ctx.fileData->RegisterLazyTimeSamples(primPath, attrName, typeName, std::move(samples));
}

template <typename T>
static VtValue ReadFlowSolutionFieldAs(int cgf,
                                       int base,
                                       int zone,
                                       int solution,
                                       const std::string& fieldName,
                                       CGNS_ENUMT(DataType_t) dataType,
                                       cgsize_t dataSize)
{
    cgsize_t rangeMin = 1;
    cgsize_t rangeMax = dataSize;
    UninitializedVtArray<T> values = MakeUninitializedVtArray<T>(static_cast<size_t>(dataSize));
    call_safe(cg_field_read(cgf, base, zone, solution, fieldName.c_str(), dataType, &rangeMin, &rangeMax, values.data),
              "read field data");
    return VtValue::Take(values.array);
}

static VtValue ReadFlowSolutionField(int cgf,
                                     int base,
                                     int zone,
                                     int solution,
                                     const std::string& fieldName,
                                     CGNS_ENUMT(DataType_t) dataType,
                                     cgsize_t dataSize)
{
    if (dataType == CGNS_ENUMV(RealSingle))
        return ReadFlowSolutionFieldAs<float>(cgf, base, zone, solution, fieldName, dataType, dataSize);
    if (dataType == CGNS_ENUMV(RealDouble))
        return ReadFlowSolutionFieldAs<double>(cgf, base, zone, solution, fieldName, dataType, dataSize);
    if (dataType == CGNS_ENUMV(Integer))
        return ReadFlowSolutionFieldAs<int>(cgf, base, zone, solution, fieldName, dataType, dataSize);
    return ReadFlowSolutionFieldAs<int64_t>(cgf, base, zone, solution, fieldName, CGNS_ENUMV(LongInteger), dataSize);
}

struct FlowSolutionFieldRegistration
{
    const ReadContext& context;
    int base;
    int zone;
    int solution;
    CGNS_ENUMT(GridLocation_t) location;
    cgsize_t numNodes;
    cgsize_t numCells;
    const std::string& solutionName;
    bool isGroupLeader;
    const SdfPath& solutionPath;
    const UsdPrim& solutionPrim;
    const BaseTimeInfo& baseTimeInfo;
    const ZoneIterativeInfo& zoneIterativeInfo;
    const cae::StringMap<int>& solutionIndices;
};

static void RegisterFlowSolutionField(const FlowSolutionFieldRegistration& registration, int field)
{
    const ReadContext& ctx = registration.context;
    const int base = registration.base;
    const int zone = registration.zone;
    const int solution = registration.solution;
    const auto location = registration.location;
    const cgsize_t numNodes = registration.numNodes;
    const cgsize_t numCells = registration.numCells;
    const std::string& solutionName = registration.solutionName;
    const bool isGroupLeader = registration.isGroupLeader;
    const SdfPath& solutionPath = registration.solutionPath;
    const UsdPrim& solutionPrim = registration.solutionPrim;
    const BaseTimeInfo& baseTimeInfo = registration.baseTimeInfo;
    const ZoneIterativeInfo& zoneIterativeInfo = registration.zoneIterativeInfo;
    const cae::StringMap<int>& solutionIndices = registration.solutionIndices;

    CGNS_ENUMT(DataType_t) dataType;
    std::array<char, 1024> name = {};
    call_safe(cg_field_info(ctx.cgFile, base, zone, solution, field, &dataType, name.data()), "read field info");
    const std::string fieldNameString(name.data());
    const TfToken fieldName(TfMakeValidIdentifier(fieldNameString));

    OmniSciArrayAPI::Apply(solutionPrim, fieldName).CreateDeviceAttr().Set(TfToken("cpu"));
    auto fieldAPI = OmniSciFieldAPI::Apply(solutionPrim, fieldName);
    fieldAPI.CreateNameAttr().Set(fieldNameString);
    fieldAPI.CreateAssociationAttr().Set(location == CGNS_ENUMV(CellCenter) ? OmniSciTokens->element :
                                                                              OmniSciTokens->node);

    const auto effectiveType = ResolveDataType(dataType, ctx.options.intSize, ctx.options.floatSize);
    const TfToken sdfType = DataTypeToSdfTypeName(effectiveType);
    const cgsize_t dataSize = location == CGNS_ENUMV(CellCenter) ? numCells : numNodes;
    const TfToken valueAttr = MakeArrayValueAttrName(fieldName);
    const auto makeLoader = [&fileName = ctx.fileName, &solutionName, &fieldNameString, base, zone, effectiveType,
                             dataSize](int stepSolution)
    {
        return MakeCGNSLoader(
            fileName, solutionName + "/" + fieldNameString,
            [base, zone, stepSolution, fieldNameString, effectiveType, dataSize](int cgf)
            { return ReadFlowSolutionField(cgf, base, zone, stepSolution, fieldNameString, effectiveType, dataSize); });
    };

    if (!isGroupLeader)
    {
        ctx.fileData->RegisterLazySingleState(
            solutionPath, valueAttr, sdfType, ResolveSampleTime(ctx, baseTimeInfo, 0), makeLoader(solution));
        return;
    }

    const auto& steps = zoneIterativeInfo.flowSolutionPointers.at(solutionName);
    std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
    for (size_t index = 0; index < steps.size(); ++index)
    {
        const auto solutionIndex = solutionIndices.find(steps[index]);
        if (solutionIndex != solutionIndices.end())
            samples.emplace_back(ResolveSampleTime(ctx, baseTimeInfo, index), makeLoader(solutionIndex->second));
    }
    RegisterTimeSamples(ctx, solutionPath, valueAttr, sdfType, std::move(samples));
}

static SdfPathVector ReadFlowSolutions(const ReadContext& ctx,
                                       int base,
                                       int zone,
                                       cgsize_t numNodes,
                                       cgsize_t numCells,
                                       const SdfPath& zonePath,
                                       const BaseTimeInfo& baseTimeInfo,
                                       const ZoneIterativeInfo& zoneIterativeInfo)
{
    int nsols = 0;
    call_safe(cg_nsols(ctx.cgFile, base, zone, &nsols), "read num solutions");
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("    [zone %d] nsols=%d\n", zone, nsols);

    const auto solNameToIndex = BuildSolutionIndexMap(ctx.cgFile, base, zone, nsols);
    SdfPathVector flowSolutionPaths;
    std::array<char, 1024> name = {};
    for (int sol = 1; sol <= nsols; ++sol)
    {
        CGNS_ENUMT(GridLocation_t) location;
        call_safe(cg_sol_info(ctx.cgFile, base, zone, sol, name.data(), &location), "read sol info");

        const std::string fsName = TfStringTrim(std::string(name.data()));
        const bool isGroupLeader = cae::Contains(zoneIterativeInfo.flowSolutionPointers, fsName);
        const bool isGroupMember = cae::Contains(zoneIterativeInfo.allFlowSolutionPointers, fsName);
        const char* locStr = (location == CGNS_ENUMV(CellCenter)) ? "CellCenter" : "Vertex";

        TF_DEBUG(CAE_CGNS_FILEFORMAT)
            .Msg("    [sol %d] name='%s' location=%s leader=%d member=%d\n", sol, fsName.c_str(), locStr,
                 (int)isGroupLeader, (int)isGroupMember);

        if (!isGroupLeader && isGroupMember)
        {
            TF_DEBUG(CAE_CGNS_FILEFORMAT)
                .Msg("    [sol %d] skipping '%s' (non-leader temporal member)\n", sol, fsName.c_str());
            continue;
        }

        const SdfPath fsPath = MakeChildPath(zonePath, name.data());
        const UsdPrim fsPrim = ctx.stage->DefinePrim(fsPath, OmniSciCgnsFileFormatTokens->CGNSFlowSolution);
        auto fsAPI = OmniSciCgnsFlowSolutionAPI::Apply(fsPrim);
        fsAPI.CreateGridLocationAttr().Set(TfToken(locStr));

        int nfields = 0;
        call_safe(cg_nfields(ctx.cgFile, base, zone, sol, &nfields), "read nfields");
        const FlowSolutionFieldRegistration registration{
            ctx,    base,          zone,   sol,    location,     numNodes,          numCells,
            fsName, isGroupLeader, fsPath, fsPrim, baseTimeInfo, zoneIterativeInfo, solNameToIndex
        };
        for (int field = 1; field <= nfields; ++field)
            RegisterFlowSolutionField(registration, field);

        flowSolutionPaths.push_back(fsPath);
    }

    return flowSolutionPaths;
}

static void ReadZone(const ReadContext& ctx, int base, int zone, const SdfPath& basePath, const BaseTimeInfo& baseTimeInfo)
{
    CGNS_ENUMT(ZoneType_t) zoneType;
    call_safe(cg_zone_type(ctx.cgFile, base, zone, &zoneType), "read zone type");
    if (zoneType == CGNS_ENUMV(Structured))
    {
        TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("  [zone %d] -> skip (Structured)\n", zone);
        return;
    }

    std::array<char, 1024> name = {};
    std::array<cgsize_t, 9> size = {};
    call_safe(cg_zone_read(ctx.cgFile, base, zone, name.data(), size.data()), "read zone");
    TF_DEBUG(CAE_CGNS_FILEFORMAT)
        .Msg( // NOSONAR: this is the documented TF_DEBUG macro syntax.
            "  [zone %d] name='%s' nNodes=%lld nCells=%lld\n", zone, name.data(), (long long)size[0], (long long)size[1]);
    if (!ctx.options.filterZoneName.empty() && ctx.options.filterZoneName != name.data())
        return;

    const SdfPath zonePath = MakeChildPath(basePath, name.data());
    const UsdPrim zonePrim = ctx.stage->DefinePrim(zonePath, OmniSciCgnsFileFormatTokens->CGNSZone);
    OmniSciCgnsZoneAPI::Apply(zonePrim);

    const double singleStateSampleTime = GetSingleStateSampleTime(ctx, baseTimeInfo);
    const SdfPathVector gridCoordinatesPaths =
        ReadGridCoordinates(ctx, base, zone, size[0], zonePath, singleStateSampleTime);
    OmniSciCgnsZoneAPI(zonePrim).CreateGridCoordinatesRel().SetTargets(gridCoordinatesPaths);

    const SdfPathVector sectionPaths = ReadSections(ctx, base, zone, zonePath, zonePrim, singleStateSampleTime);
    OmniSciCgnsZoneAPI(zonePrim).CreateSectionsRel().SetTargets(sectionPaths);

    const ZoneIterativeInfo zoneIterativeInfo = ReadZoneIterativeInfo(ctx, base, zone, baseTimeInfo.numSteps);
    const SdfPathVector flowSolutionPaths =
        ReadFlowSolutions(ctx, base, zone, size[0], size[1], zonePath, baseTimeInfo, zoneIterativeInfo);
    TF_DEBUG(CAE_CGNS_FILEFORMAT)
        .Msg("    [zone %d] wiring %zu gridCoordinates, %zu sections, %zu flowSolutions\n", zone,
             gridCoordinatesPaths.size(), sectionPaths.size(), flowSolutionPaths.size());
    OmniSciCgnsZoneAPI(zonePrim).CreateFlowSolutionsRel().SetTargets(flowSolutionPaths);
}

static void ReadBase(const ReadContext& ctx, int base, const SdfPath& scopePath)
{
    int cellDimension = 0;
    int physicalDimension = 0;
    std::array<char, 1024> name = {};
    call_safe(cg_base_read(ctx.cgFile, base, name.data(), &cellDimension, &physicalDimension), "read base");
    TF_DEBUG(CAE_CGNS_FILEFORMAT)
        .Msg( // NOSONAR: this is the documented TF_DEBUG macro syntax.
            "[base %d] name='%s' cell_dim=%d phys_dim=%d\n", base, name.data(), cellDimension, physicalDimension);
    if (cellDimension != 3)
        return;
    if (!ctx.options.filterBaseName.empty() && ctx.options.filterBaseName != name.data())
        return;

    const SdfPath basePath = MakeChildPath(scopePath, name.data());
    ctx.stage->DefinePrim(basePath, OmniSciCgnsFileFormatTokens->CGNSBase);
    const BaseTimeInfo baseTimeInfo = ReadBaseTimeInfo(ctx, base);

    int zoneCount = 0;
    call_safe(cg_nzones(ctx.cgFile, base, &zoneCount), "read num zones");
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("  [base %d] nzones=%d\n", base, zoneCount);
    for (int zone = 1; zone <= zoneCount; ++zone)
        ReadZone(ctx, base, zone, basePath, baseTimeInfo);
}

ReadCGNSResult ReadCGNS(int cgFile, const std::string& fname, const SdfLayer::FileFormatArguments& args)
{
    const ReadOptions options = ParseReadOptions(fname, args);
    ReadContext ctx = CreateReadContext(cgFile, fname, options);

    // The wrapper Scope at the filename stem is the layer's default prim;
    // CGNS Bases are authored as its children so multi-base files represent
    // every base.
    const SdfPath scopePath = options.rootPath;
    UsdGeomScope::Define(ctx.stage, scopePath);

    int nbases = 0;
    call_safe(cg_nbases(ctx.cgFile, &nbases), "read num bases");
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("[ReadCGNS] '%s' nbases=%d\n", fname.c_str(), nbases);

    for (int base = 1; base <= nbases; ++base)
        ReadBase(ctx, base, scopePath);

    return { ctx.layer, ctx.fileData };
}

ReadCGNSResult ReadCGNS(const std::string& fname, const SdfLayer::FileFormatArguments& args)
{
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("[ReadCGNS] entry fname='%s' args.size=%zu\n", fname.c_str(), args.size());
    for (const auto& [key, value] : args)
        TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("[ReadCGNS]   arg %s='%s'\n", key.c_str(), value.c_str());

    std::scoped_lock lock(GetCGNSMutex());
    int cgFile = -1;
    const int openErr = cg_open(fname.c_str(), CG_MODE_READ, &cgFile);
    if (openErr != CG_OK)
    {
        const char* cgErr = cg_get_error();
        TF_RUNTIME_ERROR("OmniSciCgnsFileFormat: cg_open('%s') failed: code=%d (%s).", fname.c_str(), openErr,
                         cgErr ? cgErr : "<no error string>");
        return {};
    }
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("[ReadCGNS] cg_open OK, cgFile=%d\n", cgFile);
    try
    {
        auto result = ReadCGNS(cgFile, fname, args);
        cg_close(cgFile);
        TF_DEBUG(CAE_CGNS_FILEFORMAT)
            .Msg("[ReadCGNS] cg_close OK; returning layer=%s fileData=%s\n", result.first ? "set" : "null",
                 result.second ? "set" : "null");
        return result;
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciCgnsFileFormat: exception during ReadCGNS('%s'): %s", fname.c_str(), ex.what());
        cg_close(cgFile);
        return {};
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// OmniSciCgnsFileFormat
// ---------------------------------------------------------------------------

OmniSciCgnsFileFormat::OmniSciCgnsFileFormat()
    : SdfFileFormat(OmniSciCgnsFileFormatTokens->Id,
                    OmniSciCgnsFileFormatTokens->Version,
                    OmniSciCgnsFileFormatTokens->Target,
                    OmniSciCgnsFileFormatTokens->Extension)
{
}

OmniSciCgnsFileFormat::~OmniSciCgnsFileFormat() = default;

bool OmniSciCgnsFileFormat::CanRead(const std::string& filePath) const
{
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("OmniSciCgnsFileFormat::CanRead('%s')\n", filePath.c_str());
    if (TfGetExtension(filePath) != OmniSciCgnsFileFormatTokens->Extension)
    {
        TF_DEBUG(CAE_CGNS_FILEFORMAT)
            .Msg("OmniSciCgnsFileFormat::CanRead: extension mismatch ('%s' != '%s')\n",
                 TfGetExtension(filePath).c_str(), OmniSciCgnsFileFormatTokens->Extension.GetText());
        return false;
    }

    try
    {
        const CaeResolverAssetPtr asset = CaeResolveAsset(filePath);
        int cgioFile = -1;
        std::scoped_lock lock(GetCGNSMutex());
        const int cgioErr = cgio_open_file(asset->LocalPath().c_str(), CGIO_MODE_READ, CGIO_FILE_NONE, &cgioFile);
        if (cgioErr == CGIO_ERR_NONE)
        {
            cgio_close_file(cgioFile);
            TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("OmniSciCgnsFileFormat::CanRead: cgio_open_file OK -> true\n");
            return true;
        }
        char cgioErrMsg[CGIO_MAX_ERROR_LENGTH + 1] = { 0 };
        cgio_error_message(cgioErrMsg);
        TF_DEBUG(CAE_CGNS_FILEFORMAT)
            .Msg("OmniSciCgnsFileFormat::CanRead: cgio_open_file failed code=%d msg='%s' -> false\n", cgioErr,
                 cgioErrMsg);
        return false;
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_DEBUG(CAE_CGNS_FILEFORMAT)
            .Msg("OmniSciCgnsFileFormat::CanRead: resolver access failed: %s -> false\n", ex.what());
        return false;
    }
}

bool OmniSciCgnsFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const
{
    TF_DEBUG(CAE_CGNS_FILEFORMAT)
        .Msg("OmniSciCgnsFileFormat::Read('%s') metadataOnly=%d layer=%p\n", resolvedPath.c_str(), metadataOnly ? 1 : 0,
             static_cast<const void*>(layer));

    if (!TF_VERIFY(layer))
        return false;

    const auto& fmtArgs = layer->GetFileFormatArguments();
    const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
    CaeResolverAssetPtr asset;
    SdfLayer::FileFormatArguments readArgs;
    try
    {
        asset = CaeOpenResolverAsset(identifier, ArResolvedPath(resolvedPath));
        readArgs = CaePrepareResolverArguments(identifier, fmtArgs);
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciCgnsFileFormat: %s", ex.what());
        return false;
    }
    TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("OmniSciCgnsFileFormat::Read fmtArgs.size=%zu\n", fmtArgs.size());
    for (const auto& [key, value] : fmtArgs)
        TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("OmniSciCgnsFileFormat::Read   arg %s='%s'\n", key.c_str(), value.c_str());

    auto result = detail::ReadCGNS(asset->LocalPath(), readArgs);
    if (!result.first || !result.second)
    {
        TF_DEBUG(CAE_CGNS_FILEFORMAT).Msg("OmniSciCgnsFileFormat::Read combined branch: incomplete result -> false\n");
        return false;
    }

    result.second->KeepAlive(asset);
    result.second->CopyFrom(_GetLayerData(*result.first));
    SdfAbstractDataRefPtr combinedData = result.second;
    _SetLayerData(layer, combinedData);

    SdfPath rootPath;
    try
    {
        rootPath = CaeResolveRootPrimPath(identifier, fmtArgs);
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciCgnsFileFormat: %s", ex.what());
        return false;
    }
    CaeAuthorMountPathOvers(layer, rootPath);
    return true;
}

void OmniSciCgnsFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                const PcpDynamicFileFormatContext& context,
                                                                FileFormatArguments* args,
                                                                VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciCgnsFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, detail::GetDynamicFileFormatArgs());
}

bool OmniSciCgnsFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciCgnsFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciCgnsFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciCgnsFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
