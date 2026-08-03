// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciEgridFileFormat.h"

#include "CaeFileFormatData.h"
#include "DynamicFileFormatArguments.h"
#include "EclipseBinaryFile.h"
#include "FileFormatError.h"
#include "MountPath.h"
#include "ResolverAsset.h"
#include "UninitializedVtArray.h"
#include "debugCodes.h"

#include <omniSci/arrayAPI.h>
#include <omniSci/dataset.h>
#include <omniSci/tokens.h>
#include <omniSciFileFormatArgs/tokens.h>
#include <omniSciReservoir/cornerPointGridAPI.h>
#include <omniSciReservoir/tokens.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciEgridFileFormatTokens, OMNI_SCI_EGRID_FILE_FORMAT_TOKENS);

namespace egrid_detail
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 1> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciEgridFileFormatTokens->ArgCacheMode },
        } };

    return DynamicFileFormatArgs;
}

static const char* CacheModeName(CaeFileFormatData::CacheMode mode)
{
    switch (mode)
    {
    case CaeFileFormatData::CacheMode::All:
        return "all";
    case CaeFileFormatData::CacheMode::Static:
        return "static";
    case CaeFileFormatData::CacheMode::None:
        return "none";
    }
    return "unknown";
}

struct ReadOptions
{
    SdfPath rootPath;
    CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All;
};

struct GridInfo
{
    std::array<int, 3> dims = { 0, 0, 0 };
    CaeEclipseRecord coord;
    CaeEclipseRecord zcorn;
    std::optional<CaeEclipseRecord> actnum;
    std::vector<double> mapAxes;
    std::string lengthUnit;
};

struct ReadContext
{
    ReadContext() = default;
    ReadContext(const ReadContext&) = delete;
    ReadContext& operator=(const ReadContext&) = delete;
    ReadContext(ReadContext&&) noexcept = default;
    ReadContext& operator=(ReadContext&&) noexcept = default;

    GridInfo grid;
    ReadOptions options;
    SdfLayerRefPtr layer;
    UsdStageRefPtr stage;
    CaeFileFormatDataRefPtr fileData;
};

using ReadEgridResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;

static size_t NumLogicalCells(const GridInfo& grid)
{
    return static_cast<size_t>(grid.dims[0]) * static_cast<size_t>(grid.dims[1]) * static_cast<size_t>(grid.dims[2]);
}

static size_t CoordValueCount(const GridInfo& grid)
{
    return 6u * static_cast<size_t>(grid.dims[0] + 1) * static_cast<size_t>(grid.dims[1] + 1);
}

static size_t ZcornValueCount(const GridInfo& grid)
{
    return 8u * NumLogicalCells(grid);
}

static void CheckExpectedCount(const std::string& keyword, size_t actual, size_t expected)
{
    if (actual != expected)
    {
        throw cae::FileFormatError(keyword + " expected " + std::to_string(expected) + " values, found " +
                                   std::to_string(actual));
    }
}

static std::array<int, 3> ParseGridDims(const CaeEclipseRecord& gridHead)
{
    const std::vector<int> values = CaeLoadEclipseIntValues(gridHead);
    // GRIDHEAD layout: values[0] is the grid type tag (0 = corner-point), and
    // (NX, NY, NZ) follow at positions 1..3.  Some emitters omit the leading
    // type tag and start dimensions at position 0; tolerate both layouts.
    if (values.size() >= 4 && values[1] > 0 && values[2] > 0 && values[3] > 0)
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("[EGRID] GRIDHEAD dims from offset 1: (%d,%d,%d)\n", values[1], values[2], values[3]);
        return { values[1], values[2], values[3] };
    }
    if (values.size() >= 3 && values[0] > 0 && values[1] > 0 && values[2] > 0)
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("[EGRID] GRIDHEAD dims from offset 0: (%d,%d,%d)\n", values[0], values[1], values[2]);
        return { values[0], values[1], values[2] };
    }
    throw cae::FileFormatError("EGRID GRIDHEAD record does not contain positive NX, NY, NZ dimensions.");
}

static std::string NormalizeLengthUnit(const std::string& unitText)
{
    const std::string unit = CaeEclipseToUpper(unitText);
    if (unit == "METRES" || unit == "METERS" || unit == "METRIC" || unit == "M")
        return "m";
    if (unit == "FEET" || unit == "FIELD" || unit == "FT")
        return "ft";
    if (unit == "CM" || unit == "CENTIMETRES" || unit == "CENTIMETERS")
        return "cm";
    return unitText;
}

static std::string ParseLengthUnit(const CaeEclipseFileIndex& index)
{
    if (const CaeEclipseRecord* gridUnit = CaeFindFirstEclipseRecord(index, "GRIDUNIT", "ENDGRID"))
    {
        const std::vector<std::string> values = CaeLoadEclipseCharValues(*gridUnit);
        if (!values.empty() && !values.front().empty())
            return NormalizeLengthUnit(values.front());
    }
    if (const CaeEclipseRecord* mapUnits = CaeFindFirstEclipseRecord(index, "MAPUNITS", "ENDGRID"))
    {
        const std::vector<std::string> values = CaeLoadEclipseCharValues(*mapUnits);
        if (!values.empty() && !values.front().empty())
            return NormalizeLengthUnit(values.front());
    }
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[EGRID] no length unit record found\n");
    return {};
}

static std::vector<double> ParseMapAxes(const CaeEclipseFileIndex& index)
{
    const CaeEclipseRecord* mapAxes = CaeFindFirstEclipseRecord(index, "MAPAXES", "ENDGRID");
    if (!mapAxes)
        return {};

    std::vector<double> values = CaeLoadEclipseDoubleValues(*mapAxes);
    if (values.size() < 6)
        return {};
    values.resize(6);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[EGRID] parsed MAPAXES\n");
    return values;
}

static GridInfo ParseEgrid(const std::string& filePath)
{
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[EGRID] ParseEgrid('%s')\n", filePath.c_str());
    // Stop indexing after ENDGRID so NNC/LGR/result tails don't enter the
    // index; the reader currently indexes only the global-grid section.
    const CaeEclipseFileIndex index = CaeIndexEclipseBinaryFile(filePath, "ENDGRID");
    const CaeEclipseRecord* gridHead = CaeFindFirstEclipseRecord(index, "GRIDHEAD", "ENDGRID");
    const CaeEclipseRecord* coord = CaeFindFirstEclipseRecord(index, "COORD", "ENDGRID");
    const CaeEclipseRecord* zcorn = CaeFindFirstEclipseRecord(index, "ZCORN", "ENDGRID");

    if (!gridHead)
        throw cae::FileFormatError("EGRID reader did not find GRIDHEAD.");
    if (!coord)
        throw cae::FileFormatError("EGRID reader did not find COORD.");
    if (!zcorn)
        throw cae::FileFormatError("EGRID reader did not find ZCORN.");

    GridInfo grid;
    grid.dims = ParseGridDims(*gridHead);
    grid.coord = *coord;
    grid.zcorn = *zcorn;
    if (const CaeEclipseRecord* actnum = CaeFindFirstEclipseRecord(index, "ACTNUM", "ENDGRID"))
        grid.actnum = *actnum;
    grid.lengthUnit = ParseLengthUnit(index);
    grid.mapAxes = ParseMapAxes(index);

    CheckExpectedCount("COORD", grid.coord.count, CoordValueCount(grid));
    CheckExpectedCount("ZCORN", grid.zcorn.count, ZcornValueCount(grid));
    if (grid.actnum)
        CheckExpectedCount("ACTNUM", grid.actnum->count, NumLogicalCells(grid));

    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[EGRID] grid summary dims=(%d,%d,%d) coord=%zu zcorn=%zu hasACTNUM=%d lengthUnit='%s' mapAxes=%zu records=%zu\n",
             grid.dims[0], grid.dims[1], grid.dims[2], grid.coord.count, grid.zcorn.count, grid.actnum ? 1 : 0,
             grid.lengthUnit.c_str(), grid.mapAxes.size(), index.records.size());
    return grid;
}

static CaeFileFormatData::Loader MakeDoubleLoader(CaeEclipseRecord record)
{
    return [record = std::move(record)]()
    {
        VtDoubleArray values = CaeLoadEclipseDoubleArray(record);
        return VtValue::Take(values);
    };
}

static CaeFileFormatData::Loader MakeIntLoader(CaeEclipseRecord record)
{
    return [record = std::move(record)]()
    {
        VtIntArray values = CaeLoadEclipseIntArray(record);
        return VtValue::Take(values);
    };
}

static VtIntArray BuildLogicalCellToActiveCellArray(const VtIntArray& actnum)
{
    if (actnum.empty())
        return {};

    UninitializedVtArray<int> result = MakeUninitializedVtArray<int>(actnum.size());
    int* mapping = result.data;
    if (!mapping)
        throw std::logic_error("Uninitialized Eclipse cell map storage is missing");

    int activeCell = 0;
    for (size_t logicalCell = 0; logicalCell < actnum.size(); ++logicalCell)
    {
        if (actnum[logicalCell] != 0)
        {
            mapping[logicalCell] = activeCell;
            ++activeCell;
        }
        else
        {
            mapping[logicalCell] = -1;
        }
    }
    return std::move(result.array);
}

static CaeFileFormatData::Loader MakeLogicalCellToActiveCellLoader(CaeEclipseRecord record)
{
    return [record = std::move(record)]()
    {
        VtIntArray actnum = CaeLoadEclipseIntArray(record);
        VtIntArray values = BuildLogicalCellToActiveCellArray(actnum);
        return VtValue::Take(values);
    };
}

static TfToken MakeArrayValueAttrName(const TfToken& arrayName)
{
    return TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

static VtDoubleArray ToVtDoubleArray(std::vector<double> values)
{
    VtDoubleArray result(values.size());
    std::copy(values.begin(), values.end(), result.begin());
    return result;
}

static ReadOptions ParseReadOptions(const std::string& filePath, const SdfLayer::FileFormatArguments& args)
{
    ReadOptions options;
    options.rootPath = CaeResolveRootPrimPath(filePath, args);
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[EGRID] options root=%s cacheMode=%s\n", options.rootPath.GetText(), CacheModeName(options.cacheMode));
    return options;
}

static ReadContext CreateReadContext(GridInfo grid, ReadOptions options)
{
    ReadContext ctx;
    ctx.grid = std::move(grid);
    ctx.options = std::move(options);
    ctx.layer = SdfLayer::CreateAnonymous();
    ctx.stage = UsdStage::Open(ctx.layer);
    UsdGeomSetStageUpAxis(ctx.stage, UsdGeomTokens->z);
    ctx.fileData = CreateCaeFileFormatData(ctx.options.cacheMode);
    return ctx;
}

static void ApplyArrayAPI(UsdPrim prim, const TfToken& arrayName)
{
    OmniSciArrayAPI arrayAPI = OmniSciArrayAPI::Apply(prim, arrayName);
    arrayAPI.CreateDeviceAttr().Set(TfToken("cpu"));
}

static void RegisterArray(
    ReadContext& ctx, UsdPrim prim, const TfToken& arrayName, const TfToken& valueType, CaeFileFormatData::Loader loader)
{
    ApplyArrayAPI(prim, arrayName);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[EGRID] register array %s type=%s root=%s\n", arrayName.GetText(), valueType.GetText(),
             ctx.options.rootPath.GetText());
    ctx.fileData->RegisterLazySingleState(
        ctx.options.rootPath, MakeArrayValueAttrName(arrayName), valueType, 0.0, std::move(loader));
}

static ReadEgridResult ReadEgrid(const std::string& filePath, const SdfLayer::FileFormatArguments& args)
{
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[EGRID] ReadEgrid('%s') args=%zu\n", filePath.c_str(), args.size());
    for (const auto& [key, value] : args)
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[EGRID]   arg %s='%s'\n", key.c_str(), value.c_str());

    GridInfo grid = ParseEgrid(filePath);
    ReadContext ctx = CreateReadContext(std::move(grid), ParseReadOptions(filePath, args));

    const UsdPrim gridPrim = OmniSciDataset::Define(ctx.stage, ctx.options.rootPath).GetPrim();
    OmniSciReservoirCornerPointGridAPI gridAPI = OmniSciReservoirCornerPointGridAPI::Apply(gridPrim);
    gridAPI.CreateLogicalCellDimsAttr().Set(GfVec3i(ctx.grid.dims[0], ctx.grid.dims[1], ctx.grid.dims[2]));
    gridAPI.CreateSourceFormatAttr().Set(OmniSciReservoirTokens->egrid);
    gridAPI.CreateIndexOrderAttr().Set(OmniSciReservoirTokens->eclipse);
    gridAPI.CreateDepthDirectionAttr().Set(OmniSciReservoirTokens->zDown);
    gridAPI.CreateNameAttr().Set(TfStringGetBeforeSuffix(TfGetBaseName(filePath)));
    if (!ctx.grid.lengthUnit.empty())
        gridAPI.CreateLengthUnitAttr().Set(ctx.grid.lengthUnit);
    if (!ctx.grid.mapAxes.empty())
        gridAPI.CreateMapAxesAttr().Set(ToVtDoubleArray(ctx.grid.mapAxes));

    RegisterArray(ctx, gridPrim, OmniSciReservoirTokens->coord, TfToken("double[]"), MakeDoubleLoader(ctx.grid.coord));
    RegisterArray(ctx, gridPrim, OmniSciReservoirTokens->zcorn, TfToken("double[]"), MakeDoubleLoader(ctx.grid.zcorn));
    if (ctx.grid.actnum)
    {
        RegisterArray(ctx, gridPrim, OmniSciReservoirTokens->actnum, TfToken("int[]"), MakeIntLoader(*ctx.grid.actnum));
        RegisterArray(ctx, gridPrim, OmniSciReservoirTokens->logicalCellToActiveCell, TfToken("int[]"),
                      MakeLogicalCellToActiveCellLoader(*ctx.grid.actnum));
    }

    return { ctx.layer, ctx.fileData };
}

static bool LooksLikeEgrid(const std::string& filePath)
{
    std::string keyword;
    std::string type;
    if (!CaeReadFirstEclipseRecordHeader(filePath, &keyword, &type))
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("[EGRID] LooksLikeEgrid('%s') -> false (no Eclipse header)\n", filePath.c_str());
        return false;
    }
    const bool result = (keyword == "FILEHEAD" || keyword == "GRIDHEAD") && (type == "INTE" || type == "LOGI");
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[EGRID] LooksLikeEgrid('%s') -> %d firstKeyword='%s' type='%s'\n", filePath.c_str(), result ? 1 : 0,
             keyword.c_str(), type.c_str());
    return result;
}

} // namespace egrid_detail

OmniSciEgridFileFormat::OmniSciEgridFileFormat()
    : SdfFileFormat(OmniSciEgridFileFormatTokens->Id,
                    OmniSciEgridFileFormatTokens->Version,
                    OmniSciEgridFileFormatTokens->Target,
                    OmniSciEgridFileFormatTokens->Extension)
{
}

OmniSciEgridFileFormat::~OmniSciEgridFileFormat() = default;

bool OmniSciEgridFileFormat::CanRead(const std::string& filePath) const
{
    const std::string ext = CaeEclipseToLower(TfGetExtension(filePath));
    if (ext != OmniSciEgridFileFormatTokens->Extension)
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("OmniSciEgridFileFormat::CanRead('%s') -> false (extension='%s')\n", filePath.c_str(), ext.c_str());
        return false;
    }
    try
    {
        const CaeResolverAssetPtr asset = CaeResolveAsset(filePath);
        const bool result = egrid_detail::LooksLikeEgrid(asset->LocalPath());
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("OmniSciEgridFileFormat::CanRead('%s') -> %d\n", filePath.c_str(), result ? 1 : 0);
        return result;
    }
    catch (const cae::FileFormatError&)
    {
        return false;
    }
}

bool OmniSciEgridFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool /*metadataOnly*/) const
{
    if (!TF_VERIFY(layer))
        return false;

    const auto& fmtArgs = layer->GetFileFormatArguments();
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("OmniSciEgridFileFormat::Read('%s')\n", resolvedPath.c_str());

    try
    {
        const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
        const CaeResolverAssetPtr asset = CaeOpenResolverAsset(identifier, ArResolvedPath(resolvedPath));
        const auto readArgs = CaePrepareResolverArguments(identifier, fmtArgs);
        egrid_detail::ReadEgridResult result = egrid_detail::ReadEgrid(asset->LocalPath(), readArgs);
        if (!result.first || !result.second)
            return false;

        result.second->KeepAlive(asset);
        result.second->CopyFrom(_GetLayerData(*result.first));
        SdfAbstractDataRefPtr fileData = result.second;
        _SetLayerData(layer, fileData);

        const SdfPath rootPath = CaeResolveRootPrimPath(identifier, fmtArgs);
        CaeAuthorMountPathOvers(layer, rootPath);
        return true;
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciEgridFileFormat: %s", ex.what());
        return false;
    }
}

void OmniSciEgridFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                 const PcpDynamicFileFormatContext& context,
                                                                 FileFormatArguments* args,
                                                                 VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, egrid_detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciEgridFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, egrid_detail::GetDynamicFileFormatArgs());
}

bool OmniSciEgridFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciEgridFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciEgridFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciEgridFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
