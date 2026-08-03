// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciInitFileFormat.h"

#include "CaeFileFormatData.h"
#include "ContainerUtils.h"
#include "DynamicFileFormatArguments.h"
#include "EclipseBinaryFile.h"
#include "FileFormatError.h"
#include "MountPath.h"
#include "ResolverAsset.h"
#include "debugCodes.h"

#include <omniSci/arrayAPI.h>
#include <omniSci/dataset.h>
#include <omniSci/fieldAPI.h>
#include <omniSci/tokens.h>
#include <omniSciFileFormatArgs/tokens.h>
#include <omniSciReservoir/cellPropertyAPI.h>
#include <omniSciReservoir/tokens.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
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
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciInitFileFormatTokens, OMNI_SCI_INIT_FILE_FORMAT_TOKENS);

namespace init_detail
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 2> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciInitFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatReservoirKeywordMode,
              OmniSciInitFileFormatTokens->ArgReservoirKeywordMode },
        } };

    return DynamicFileFormatArgs;
}

enum class KeywordMode
{
    // Conservative default: expose only the stored static cell keywords we
    // explicitly understand as reservoir cell properties.
    Whitelist,
    // Escape hatch for project-specific INIT records.  Still requires the
    // record length to match logical or active cell count.
    AllCellSized
};

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

static const char* KeywordModeName(KeywordMode mode)
{
    switch (mode)
    {
    case KeywordMode::Whitelist:
        return "whitelist";
    case KeywordMode::AllCellSized:
        return "allCellSized";
    }
    return "unknown";
}

struct ReadOptions
{
    SdfPath rootPath;
    CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All;
    KeywordMode keywordMode = KeywordMode::Whitelist;
};

struct CellRecord
{
    CaeEclipseRecord record;
    bool integer = false;
    TfToken indexSpace;
    TfToken instanceName;
};

struct InitInfo
{
    std::array<int, 3> dims = { 0, 0, 0 };
    size_t logicalCellCount = 0;
    size_t activeCellCount = 0;
    std::vector<CellRecord> fields;
};

struct ReadContext
{
    ReadContext() = default;
    ReadContext(const ReadContext&) = delete;
    ReadContext& operator=(const ReadContext&) = delete;
    ReadContext(ReadContext&&) noexcept = default;
    ReadContext& operator=(ReadContext&&) noexcept = default;

    InitInfo info;
    ReadOptions options;
    SdfLayerRefPtr layer;
    UsdStageRefPtr stage;
    CaeFileFormatDataRefPtr fileData;
};

using ReadInitResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;

static TfToken MakeArrayValueAttrName(const TfToken& arrayName)
{
    return TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

static KeywordMode ParseKeywordMode(const SdfLayer::FileFormatArguments& args)
{
    const auto it = args.find(OmniSciInitFileFormatTokens->ArgReservoirKeywordMode.GetString());
    if (it == args.end() || it->second.empty())
        return KeywordMode::Whitelist;

    const std::string value = CaeEclipseToLower(it->second);
    if (value == "whitelist")
        return KeywordMode::Whitelist;
    if (value == "allcellsized")
        return KeywordMode::AllCellSized;

    TF_WARN("OmniSciInitFileFormat: unknown reservoirKeywordMode '%s'; using whitelist.", it->second.c_str());
    return KeywordMode::Whitelist;
}

static ReadOptions ParseReadOptions(const std::string& filePath, const SdfLayer::FileFormatArguments& args)
{
    ReadOptions options;
    options.rootPath = CaeResolveRootPrimPath(filePath, args);
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);
    options.keywordMode = ParseKeywordMode(args);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[INIT] options root=%s cacheMode=%s keywordMode=%s\n", options.rootPath.GetText(),
             CacheModeName(options.cacheMode), KeywordModeName(options.keywordMode));
    return options;
}

static ReadContext CreateReadContext(InitInfo info, ReadOptions options)
{
    ReadContext ctx;
    ctx.info = std::move(info);
    ctx.options = std::move(options);
    ctx.layer = SdfLayer::CreateAnonymous();
    ctx.stage = UsdStage::Open(ctx.layer);
    UsdGeomSetStageUpAxis(ctx.stage, UsdGeomTokens->z);
    ctx.fileData = CreateCaeFileFormatData(ctx.options.cacheMode);
    return ctx;
}

static std::array<int, 3> ParseDimsFromIntehead(const CaeEclipseRecord& record)
{
    const std::vector<int> values = CaeLoadEclipseIntValues(record);
    // Eclipse INTEHEAD uses zero-based positions 8, 9, 10 for NX, NY, NZ in
    // the simulator outputs this reader targets.  The same positions are used
    // by INIT and UNRST, which lets result overlays classify cell-sized arrays
    // without opening the matching EGRID file.
    if (values.size() > 10 && values[8] > 0 && values[9] > 0 && values[10] > 0)
        return { values[8], values[9], values[10] };
    throw cae::FileFormatError("INIT INTEHEAD record does not contain positive NX, NY, NZ dimensions.");
}

static size_t ParseActiveCellCountFromIntehead(const CaeEclipseRecord& record)
{
    const std::vector<int> values = CaeLoadEclipseIntValues(record);
    // INTEHEAD[11] stores the number of active cells in the tested Eclipse
    // outputs.  We use it only for packing metadata; no active-to-logical map is
    // synthesized here because that requires ACTNUM/EGRID context and belongs
    // in a higher-level consumer such as DAV.
    if (values.size() > 11 && values[11] > 0)
        return static_cast<size_t>(values[11]);
    return 0;
}

static size_t NumLogicalCells(const std::array<int, 3>& dims)
{
    return static_cast<size_t>(dims[0]) * static_cast<size_t>(dims[1]) * static_cast<size_t>(dims[2]);
}

static std::optional<TfToken> ClassifyIndexSpace(size_t count, size_t logicalCellCount, size_t activeCellCount)
{
    // Preserve source packing.  INIT commonly stores properties in active-cell
    // order, and expanding those values would require a grid activity map that
    // this standalone overlay layer intentionally does not own.
    if (count == logicalCellCount)
        return OmniSciReservoirTokens->logicalCells;
    if (activeCellCount > 0 && count == activeCellCount)
        return OmniSciReservoirTokens->activeCells;
    return std::nullopt;
}

static bool IsWhitelistedDoubleKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "PORV",  "DEPTH", "DX",    "DY",    "DZ",     "PORO",  "PERMX",  "PERMY", "PERMZ",    "NTG",
        "TRANX", "TRANY", "TRANZ", "MULTX", "MULTX-", "MULTY", "MULTY-", "MULTZ", "SWATINIT", "SWL",
        "SWCR",  "SWU",   "SGL",   "SGCR",  "SGU",    "SOWCR", "SOGCR",  "KRWR",  "KRGR",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsWhitelistedIntKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "ENDNUM", "EQLNUM", "FIPNUM", "FLUXNUM", "SATNUM", "IMBNUM", "PVTNUM", "ROCKNUM",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsKnownNonCellKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "INTEHEAD", "LOGIHEAD", "DOUBHEAD", "TABDIMS", "TAB", "FILEHEAD", "GRIDHEAD",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsSelectedRecord(const CaeEclipseRecord& record, KeywordMode mode, bool* integer)
{
    if (!integer)
        return false;

    if (mode == KeywordMode::Whitelist)
    {
        if (IsWhitelistedIntKeyword(record.keyword) && CaeEclipseIsIntegerRecord(record))
        {
            *integer = true;
            return true;
        }
        if (IsWhitelistedDoubleKeyword(record.keyword) && CaeEclipseIsFloatingPointRecord(record))
        {
            *integer = false;
            return true;
        }
        return false;
    }

    if (IsKnownNonCellKeyword(record.keyword) || !CaeEclipseIsNumericRecord(record))
        return false;

    *integer = CaeEclipseIsIntegerRecord(record);
    return true;
}

static TfToken MakeUniqueInstanceName(const std::string& keyword, cae::StringUnorderedMap<int>* counts)
{
    std::string base = TfMakeValidIdentifier(keyword);
    if (base.empty())
        base = "field";

    int& count = (*counts)[base];
    if (count++ == 0)
        return TfToken(base);
    return TfToken(base + "_" + std::to_string(count));
}

static InitInfo ParseInit(const std::string& filePath, KeywordMode keywordMode)
{
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[INIT] ParseInit('%s') keywordMode=%s\n", filePath.c_str(), KeywordModeName(keywordMode));
    const CaeEclipseFileIndex index = CaeIndexEclipseBinaryFile(filePath);
    const CaeEclipseRecord* intehead = CaeFindFirstEclipseRecord(index, "INTEHEAD");
    if (!intehead)
        throw cae::FileFormatError("INIT reader did not find INTEHEAD.");

    InitInfo info;
    info.dims = ParseDimsFromIntehead(*intehead);
    info.logicalCellCount = NumLogicalCells(info.dims);
    info.activeCellCount = ParseActiveCellCountFromIntehead(*intehead);
    if (info.activeCellCount == 0)
        info.activeCellCount = info.logicalCellCount;
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[INIT] dims=(%d,%d,%d) logicalCells=%zu activeCells=%zu records=%zu\n", info.dims[0], info.dims[1],
             info.dims[2], info.logicalCellCount, info.activeCellCount, index.records.size());

    cae::StringUnorderedMap<int> instanceCounts;
    cae::StringUnorderedSet selectedKeywords;
    for (const CaeEclipseRecord& record : index.records)
    {
        bool integer = false;
        if (!IsSelectedRecord(record, keywordMode, &integer))
            continue;

        const std::optional<TfToken> indexSpace =
            ClassifyIndexSpace(record.count, info.logicalCellCount, info.activeCellCount);
        if (!indexSpace)
        {
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[INIT] skip keyword='%s' count=%zu: not logical/active cell sized\n", record.keyword.c_str(),
                     record.count);
            continue;
        }

        // INIT should contain one stored record per static keyword.  If a file
        // repeats a keyword, keep the first occurrence for deterministic v1
        // behavior instead of guessing how later records should override it.
        if (!selectedKeywords.insert(record.keyword).second)
        {
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[INIT] skip duplicate keyword='%s'\n", record.keyword.c_str());
            continue;
        }

        CellRecord field;
        field.record = record;
        field.integer = integer;
        field.indexSpace = *indexSpace;
        field.instanceName = MakeUniqueInstanceName(record.keyword, &instanceCounts);
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("[INIT] selected keyword='%s' type=%s count=%zu indexSpace=%s instance=%s\n", record.keyword.c_str(),
                 integer ? "int" : "double", record.count, field.indexSpace.GetText(), field.instanceName.GetText());
        info.fields.push_back(std::move(field));
    }

    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[INIT] selected %zu field(s)\n", info.fields.size());
    return info;
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

static void ApplyArrayAPI(UsdPrim prim, const TfToken& arrayName)
{
    OmniSciArrayAPI arrayAPI = OmniSciArrayAPI::Apply(prim, arrayName);
    arrayAPI.CreateDeviceAttr().Set(TfToken("cpu"));
}

static void AuthorFieldStructure(UsdPrim prim, const CellRecord& field)
{
    OmniSciFieldAPI fieldAPI = OmniSciFieldAPI::Apply(prim, field.instanceName);
    fieldAPI.CreateNameAttr().Set(field.record.keyword);
    fieldAPI.CreateAssociationAttr().Set(OmniSciTokens->element);

    ApplyArrayAPI(prim, field.instanceName);

    OmniSciReservoirCellPropertyAPI propertyAPI = OmniSciReservoirCellPropertyAPI::Apply(prim, field.instanceName);
    propertyAPI.CreateIndexSpaceAttr().Set(field.indexSpace);
    propertyAPI.CreateSourceKeywordAttr().Set(field.record.keyword);
}

static void RegisterFieldData(ReadContext& ctx, const CellRecord& field)
{
    const TfToken typeName = field.integer ? TfToken("int[]") : TfToken("double[]");
    CaeFileFormatData::Loader loader = field.integer ? MakeIntLoader(field.record) : MakeDoubleLoader(field.record);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[INIT] register field '%s' attr=%s type=%s count=%zu\n", field.record.keyword.c_str(),
             field.instanceName.GetText(), typeName.GetText(), field.record.count);
    ctx.fileData->RegisterLazySingleState(
        ctx.options.rootPath, MakeArrayValueAttrName(field.instanceName), typeName, 0.0, std::move(loader));
}

static ReadInitResult ReadInit(const std::string& filePath, const SdfLayer::FileFormatArguments& args)
{
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[INIT] ReadInit('%s') args=%zu\n", filePath.c_str(), args.size());
    for (const auto& [key, value] : args)
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[INIT]   arg %s='%s'\n", key.c_str(), value.c_str());

    ReadOptions options = ParseReadOptions(filePath, args);
    InitInfo info = ParseInit(filePath, options.keywordMode);
    ReadContext ctx = CreateReadContext(std::move(info), std::move(options));

    // Overlay only: define the dataset prim and attach fields on it. The
    // matching GRDECL/EGRID layer is expected to provide grid geometry at the
    // same prim path through normal USD composition.
    const UsdPrim prim = OmniSciDataset::Define(ctx.stage, ctx.options.rootPath).GetPrim();

    for (const CellRecord& field : ctx.info.fields)
    {
        AuthorFieldStructure(prim, field);
        RegisterFieldData(ctx, field);
    }

    return { ctx.layer, ctx.fileData };
}

static bool LooksLikeInit(const std::string& filePath)
{
    std::string keyword;
    std::string type;
    if (!CaeReadFirstEclipseRecordHeader(filePath, &keyword, &type))
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[INIT] LooksLikeInit('%s') -> false (no Eclipse header)\n", filePath.c_str());
        return false;
    }
    const bool result = keyword == "INTEHEAD" && type == "INTE";
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[INIT] LooksLikeInit('%s') -> %d firstKeyword='%s' type='%s'\n", filePath.c_str(), result ? 1 : 0,
             keyword.c_str(), type.c_str());
    return result;
}

} // namespace init_detail

OmniSciInitFileFormat::OmniSciInitFileFormat()
    : SdfFileFormat(OmniSciInitFileFormatTokens->Id,
                    OmniSciInitFileFormatTokens->Version,
                    OmniSciInitFileFormatTokens->Target,
                    OmniSciInitFileFormatTokens->Extension)
{
}

OmniSciInitFileFormat::~OmniSciInitFileFormat() = default;

bool OmniSciInitFileFormat::CanRead(const std::string& filePath) const
{
    const std::string ext = CaeEclipseToLower(TfGetExtension(filePath));
    if (ext != OmniSciInitFileFormatTokens->Extension)
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("OmniSciInitFileFormat::CanRead('%s') -> false (extension='%s')\n", filePath.c_str(), ext.c_str());
        return false;
    }
    try
    {
        const CaeResolverAssetPtr asset = CaeResolveAsset(filePath);
        const bool result = init_detail::LooksLikeInit(asset->LocalPath());
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("OmniSciInitFileFormat::CanRead('%s') -> %d\n", filePath.c_str(), result ? 1 : 0);
        return result;
    }
    catch (const cae::FileFormatError&)
    {
        return false;
    }
}

bool OmniSciInitFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool /*metadataOnly*/) const
{
    if (!TF_VERIFY(layer))
        return false;

    const auto& fmtArgs = layer->GetFileFormatArguments();
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("OmniSciInitFileFormat::Read('%s')\n", resolvedPath.c_str());

    try
    {
        const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
        const CaeResolverAssetPtr asset = CaeOpenResolverAsset(identifier, ArResolvedPath(resolvedPath));
        const auto readArgs = CaePrepareResolverArguments(identifier, fmtArgs);
        init_detail::ReadInitResult result = init_detail::ReadInit(asset->LocalPath(), readArgs);
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
        TF_RUNTIME_ERROR("OmniSciInitFileFormat: %s", ex.what());
        return false;
    }
}

void OmniSciInitFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                const PcpDynamicFileFormatContext& context,
                                                                FileFormatArguments* args,
                                                                VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, init_detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciInitFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, init_detail::GetDynamicFileFormatArgs());
}

bool OmniSciInitFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciInitFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciInitFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciInitFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
