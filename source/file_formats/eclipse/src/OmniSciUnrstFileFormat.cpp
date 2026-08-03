// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciUnrstFileFormat.h"

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

TF_DEFINE_PUBLIC_TOKENS(OmniSciUnrstFileFormatTokens, OMNI_SCI_UNRST_FILE_FORMAT_TOKENS);

namespace unrst_detail
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 5> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciUnrstFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatReservoirKeywordMode,
              OmniSciUnrstFileFormatTokens->ArgReservoirKeywordMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeScale, OmniSciUnrstFileFormatTokens->ArgTimeScale },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeOffset, OmniSciUnrstFileFormatTokens->ArgTimeOffset },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeSource, OmniSciUnrstFileFormatTokens->ArgTimeSource },
        } };

    return DynamicFileFormatArgs;
}

enum class KeywordMode
{
    // Conservative default: expose only stored restart keywords we explicitly
    // support. Derived convenience fields such as SOIL are left to consumers.
    Whitelist,
    // Escape hatch for stored numeric project-specific cell arrays. This still
    // filters by logical/active cell count and skips known control records.
    AllCellSized
};

enum class TimeSource
{
    // Zero-based sample index. Multiplied by `timeScale` (the per-step dt in
    // seconds when real-time playback is desired).
    TimeStep,
    // DOUBHEAD[0] (elapsed simulation days). The default plugin `timeScale`
    // of 86400 converts that into simulation seconds.
    TimeValue,
    // SEQNUM report-step counter. The user picks `timeScale` if a real-time
    // interpretation is desired.
    IterationValue
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

static const char* TimeSourceName(TimeSource source)
{
    switch (source)
    {
    case TimeSource::TimeStep:
        return "TimeStep";
    case TimeSource::TimeValue:
        return "TimeValue";
    case TimeSource::IterationValue:
        return "IterationValue";
    }
    return "unknown";
}

struct ReadOptions
{
    SdfPath rootPath;
    CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All;
    KeywordMode keywordMode = KeywordMode::Whitelist;
    // Default maps DOUBHEAD[0] (simulation days) to simulation seconds.
    TimeSource timeSource = TimeSource::TimeValue;
    double timeScale = 86400.0;
    double timeOffset = 0.0;
};

struct RecordSample
{
    double time = 0.0;
    CaeEclipseRecord record;
};

struct TimeField
{
    std::string keyword;
    bool integer = false;
    TfToken indexSpace;
    TfToken instanceName;
    std::vector<RecordSample> samples;
};

struct CurrentStep
{
    bool valid = false;
    size_t sampleIndex = 0;
    int reportStep = 0;
    double simulationDays = 0.0;
};

struct UnrstInfo
{
    std::array<int, 3> dims = { 0, 0, 0 };
    size_t logicalCellCount = 0;
    size_t activeCellCount = 0;
    std::vector<TimeField> fields;
    std::vector<double> times;
};

struct ReadContext
{
    ReadContext() = default;
    ReadContext(const ReadContext&) = delete;
    ReadContext& operator=(const ReadContext&) = delete;
    ReadContext(ReadContext&&) noexcept = default;
    ReadContext& operator=(ReadContext&&) noexcept = default;

    UnrstInfo info;
    ReadOptions options;
    SdfLayerRefPtr layer;
    UsdStageRefPtr stage;
    CaeFileFormatDataRefPtr fileData;
};

using ReadUnrstResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;

static TfToken MakeArrayValueAttrName(const TfToken& arrayName)
{
    return TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

static KeywordMode ParseKeywordMode(const SdfLayer::FileFormatArguments& args)
{
    const auto it = args.find(OmniSciUnrstFileFormatTokens->ArgReservoirKeywordMode.GetString());
    if (it == args.end() || it->second.empty())
        return KeywordMode::Whitelist;

    const std::string value = CaeEclipseToLower(it->second);
    if (value == "whitelist")
        return KeywordMode::Whitelist;
    if (value == "allcellsized")
        return KeywordMode::AllCellSized;

    TF_WARN("OmniSciUnrstFileFormat: unknown reservoirKeywordMode '%s'; using whitelist.", it->second.c_str());
    return KeywordMode::Whitelist;
}

static TimeSource ParseTimeSource(const SdfLayer::FileFormatArguments& args, TimeSource defaultSource)
{
    const auto it = args.find(OmniSciUnrstFileFormatTokens->ArgTimeSource.GetString());
    if (it == args.end() || it->second.empty())
        return defaultSource;

    const std::string value = CaeEclipseToLower(it->second);
    if (value == "timestep")
        return TimeSource::TimeStep;
    if (value == "timevalue")
        return TimeSource::TimeValue;
    if (value == "iterationvalue")
        return TimeSource::IterationValue;

    TF_WARN("OmniSciUnrstFileFormat: unknown timeSource '%s'; using default.", it->second.c_str());
    return defaultSource;
}

static double ParseDoubleArg(const SdfLayer::FileFormatArguments& args, const TfToken& argName, double defaultValue)
{
    const auto it = args.find(argName.GetString());
    if (it == args.end() || it->second.empty())
        return defaultValue;

    try
    {
        return std::stod(it->second);
    }
    catch (const std::invalid_argument&)
    {
        TF_WARN("OmniSciUnrstFileFormat: invalid %s value '%s'; using default %g.", argName.GetText(),
                it->second.c_str(), defaultValue);
        return defaultValue;
    }
    catch (const std::out_of_range&)
    {
        TF_WARN("OmniSciUnrstFileFormat: invalid %s value '%s'; using default %g.", argName.GetText(),
                it->second.c_str(), defaultValue);
        return defaultValue;
    }
}

static ReadOptions ParseReadOptions(const std::string& filePath, const SdfLayer::FileFormatArguments& args)
{
    ReadOptions options;
    options.rootPath = CaeResolveRootPrimPath(filePath, args);
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);
    options.keywordMode = ParseKeywordMode(args);
    options.timeSource = ParseTimeSource(args, options.timeSource);
    options.timeScale = ParseDoubleArg(args, OmniSciUnrstFileFormatTokens->ArgTimeScale, options.timeScale);
    options.timeOffset = ParseDoubleArg(args, OmniSciUnrstFileFormatTokens->ArgTimeOffset, options.timeOffset);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[UNRST] options root=%s cacheMode=%s keywordMode=%s timeSource=%s timeScale=%g timeOffset=%g\n",
             options.rootPath.GetText(), CacheModeName(options.cacheMode), KeywordModeName(options.keywordMode),
             TimeSourceName(options.timeSource), options.timeScale, options.timeOffset);
    return options;
}

static ReadContext CreateReadContext(UnrstInfo info, ReadOptions options)
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
    // the simulator outputs this reader targets. These dimensions are used
    // only to classify stored result array packing.
    if (values.size() > 10 && values[8] > 0 && values[9] > 0 && values[10] > 0)
        return { values[8], values[9], values[10] };
    throw cae::FileFormatError("UNRST INTEHEAD record does not contain positive NX, NY, NZ dimensions.");
}

static size_t ParseActiveCellCountFromIntehead(const CaeEclipseRecord& record)
{
    const std::vector<int> values = CaeLoadEclipseIntValues(record);
    // INTEHEAD[11] stores active cell count in the tested Eclipse outputs.
    // UNRST values remain active-cell packed; no globalCellIndex is generated
    // here because that requires grid activity context outside this file.
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
    // Preserve native packing. Most UNRST solution arrays are active-cell
    // sized, and expanding them in the file format layer would silently bake in
    // grid context that may be composed separately.
    if (count == logicalCellCount)
        return OmniSciReservoirTokens->logicalCells;
    if (activeCellCount > 0 && count == activeCellCount)
        return OmniSciReservoirTokens->activeCells;
    return std::nullopt;
}

static bool IsWhitelistedDoubleKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "PRESSURE", "SWAT", "SGAS", "RS", "RV", "PBUB", "PDEW",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsKnownNonCellKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "SEQNUM",   "INTEHEAD", "LOGIHEAD", "DOUBHEAD", "STARTSOL", "ENDSOL", "FILEHEAD",
        "FIPFAMNA", "IGRP",     "SGRP",     "XGRP",     "ZGRP",     "IWEL",   "SWEL",
        "XWEL",     "ZWEL",     "ICON",     "SCON",     "XCON",     "ZWLS",   "IWLS",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsSelectedRecord(const CaeEclipseRecord& record, KeywordMode mode, bool* integer)
{
    if (!integer)
        return false;

    if (mode == KeywordMode::Whitelist)
    {
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

static int ParseSeqnum(const CaeEclipseRecord& record)
{
    const std::vector<int> values = CaeLoadEclipseIntValues(record);
    return values.empty() ? 0 : values.front();
}

static double ParseSimulationDays(const CaeEclipseRecord& record)
{
    const std::vector<double> values = CaeLoadEclipseDoubleValues(record);
    return values.empty() ? 0.0 : values.front();
}

static double ResolveSampleTime(const CurrentStep& step, TimeSource source, double scale, double offset)
{
    double raw = 0.0;
    switch (source)
    {
    case TimeSource::TimeStep:
        raw = static_cast<double>(step.sampleIndex);
        break;
    case TimeSource::TimeValue:
        raw = step.simulationDays;
        break;
    case TimeSource::IterationValue:
        raw = static_cast<double>(step.reportStep);
        break;
    }
    return raw * scale + offset;
}

static TimeField* FindOrCreateField(const CaeEclipseRecord& record,
                                    bool integer,
                                    const TfToken& indexSpace,
                                    std::vector<TimeField>* fields,
                                    cae::StringUnorderedMap<size_t>* fieldMap,
                                    cae::StringUnorderedMap<int>* instanceCounts)
{
    const auto found = fieldMap->find(record.keyword);
    if (found != fieldMap->end())
        return &(*fields)[found->second];

    TimeField field;
    field.keyword = record.keyword;
    field.integer = integer;
    field.indexSpace = indexSpace;
    field.instanceName = MakeUniqueInstanceName(record.keyword, instanceCounts);

    const size_t index = fields->size();
    fields->push_back(std::move(field));
    (*fieldMap)[record.keyword] = index;
    return &fields->back();
}

static UnrstInfo ParseUnrst(
    const std::string& filePath, KeywordMode keywordMode, TimeSource timeSource, double timeScale, double timeOffset)
{
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[UNRST] ParseUnrst('%s') keywordMode=%s timeSource=%s timeScale=%g timeOffset=%g\n", filePath.c_str(),
             KeywordModeName(keywordMode), TimeSourceName(timeSource), timeScale, timeOffset);
    const CaeEclipseFileIndex index = CaeIndexEclipseBinaryFile(filePath);

    UnrstInfo info;
    CurrentStep step;
    cae::StringUnorderedMap<size_t> fieldMap;
    cae::StringUnorderedMap<int> instanceCounts;

    for (const CaeEclipseRecord& record : index.records)
    {
        if (record.keyword == "SEQNUM")
        {
            // SEQNUM starts a new restart report step. The corresponding
            // DOUBHEAD usually follows shortly after and may update the time
            // when the caller selected TimeValue.
            step.valid = true;
            step.sampleIndex = info.times.size();
            step.reportStep = ParseSeqnum(record);
            step.simulationDays = static_cast<double>(step.sampleIndex);
            info.times.push_back(ResolveSampleTime(step, timeSource, timeScale, timeOffset));
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[UNRST] new report sample index=%zu reportStep=%d initialTime=%g\n", step.sampleIndex,
                     step.reportStep, info.times.back());
            continue;
        }

        if (!step.valid)
        {
            // Some restart-like files may omit an initial SEQNUM. Keep the
            // layer readable by treating the first record group as sample 0.
            step.valid = true;
            step.sampleIndex = info.times.size();
            step.reportStep = static_cast<int>(step.sampleIndex);
            step.simulationDays = static_cast<double>(step.sampleIndex);
            info.times.push_back(ResolveSampleTime(step, timeSource, timeScale, timeOffset));
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[UNRST] synthesized sample index=%zu time=%g before first SEQNUM\n", step.sampleIndex,
                     info.times.back());
        }

        if (record.keyword == "INTEHEAD")
        {
            if (info.logicalCellCount == 0)
            {
                info.dims = ParseDimsFromIntehead(record);
                info.logicalCellCount = NumLogicalCells(info.dims);
                info.activeCellCount = ParseActiveCellCountFromIntehead(record);
                if (info.activeCellCount == 0)
                    info.activeCellCount = info.logicalCellCount;
                TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                    .Msg("[UNRST] dims=(%d,%d,%d) logicalCells=%zu activeCells=%zu\n", info.dims[0], info.dims[1],
                         info.dims[2], info.logicalCellCount, info.activeCellCount);
            }
            continue;
        }

        if (record.keyword == "DOUBHEAD")
        {
            // DOUBHEAD[0] is elapsed simulation days in the Eclipse restart
            // files we support. Update the sample time after the report header
            // has been read whenever the chosen source uses TimeValue.
            step.simulationDays = ParseSimulationDays(record);
            if (timeSource == TimeSource::TimeValue && !info.times.empty())
                info.times.back() = ResolveSampleTime(step, timeSource, timeScale, timeOffset);
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[UNRST] DOUBHEAD simulationDays=%g sampleIndex=%zu resolvedTime=%g\n", step.simulationDays,
                     step.sampleIndex, info.times.empty() ? 0.0 : info.times.back());
            continue;
        }

        if (info.logicalCellCount == 0)
            continue;

        bool integer = false;
        if (!IsSelectedRecord(record, keywordMode, &integer))
            continue;

        const std::optional<TfToken> indexSpace =
            ClassifyIndexSpace(record.count, info.logicalCellCount, info.activeCellCount);
        if (!indexSpace)
        {
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[UNRST] skip keyword='%s' count=%zu: not logical/active cell sized\n", record.keyword.c_str(),
                     record.count);
            continue;
        }

        TimeField* field = FindOrCreateField(record, integer, *indexSpace, &info.fields, &fieldMap, &instanceCounts);
        field->samples.push_back({ ResolveSampleTime(step, timeSource, timeScale, timeOffset), record });
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("[UNRST] selected keyword='%s' type=%s count=%zu indexSpace=%s time=%g samplesForField=%zu\n",
                 record.keyword.c_str(), integer ? "int" : "double", record.count, indexSpace->GetText(),
                 field->samples.back().time, field->samples.size());
    }

    if (info.logicalCellCount == 0)
        throw cae::FileFormatError("UNRST reader did not find INTEHEAD.");

    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[UNRST] summary samples=%zu fields=%zu records=%zu\n", info.times.size(), info.fields.size(),
             index.records.size());
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

static void AuthorFieldStructure(UsdPrim prim, const TimeField& field)
{
    OmniSciFieldAPI fieldAPI = OmniSciFieldAPI::Apply(prim, field.instanceName);
    fieldAPI.CreateNameAttr().Set(field.keyword);
    fieldAPI.CreateAssociationAttr().Set(OmniSciTokens->element);

    ApplyArrayAPI(prim, field.instanceName);

    OmniSciReservoirCellPropertyAPI propertyAPI = OmniSciReservoirCellPropertyAPI::Apply(prim, field.instanceName);
    propertyAPI.CreateIndexSpaceAttr().Set(field.indexSpace);
    propertyAPI.CreateSourceKeywordAttr().Set(field.keyword);
}

static bool TimeSamplesAreSorted(const std::vector<std::pair<double, CaeFileFormatData::Loader>>& samples)
{
    for (size_t i = 1; i < samples.size(); ++i)
    {
        if (samples[i].first < samples[i - 1].first)
            return false;
    }
    return true;
}

static void RegisterFieldData(ReadContext& ctx, const TimeField& field)
{
    const TfToken typeName = field.integer ? TfToken("int[]") : TfToken("double[]");
    std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
    samples.reserve(field.samples.size());
    for (const RecordSample& sample : field.samples)
    {
        CaeFileFormatData::Loader loader = field.integer ? MakeIntLoader(sample.record) : MakeDoubleLoader(sample.record);
        samples.emplace_back(sample.time, std::move(loader));
    }

    const TfToken attrName = MakeArrayValueAttrName(field.instanceName);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[UNRST] register field '%s' attr=%s type=%s samples=%zu sorted=%d\n", field.keyword.c_str(),
             field.instanceName.GetText(), typeName.GetText(), samples.size(), TimeSamplesAreSorted(samples) ? 1 : 0);
    if (TimeSamplesAreSorted(samples))
        ctx.fileData->RegisterLazyTimeSamplesSorted(ctx.options.rootPath, attrName, typeName, std::move(samples));
    else
        ctx.fileData->RegisterLazyTimeSamples(ctx.options.rootPath, attrName, typeName, std::move(samples));
}

static void AuthorTimeCodes(UsdStageRefPtr stage, const UnrstInfo& info)
{
    if (!stage)
        return;
    // Self-describe the canonical sim-seconds unit on every layer that carries
    // time samples. Hosts composing this layer through payload/sublayer/ref
    // arcs are responsible for authoring an Sdf.LayerOffset matching their
    // own stage TCPS (the project ships no helper API for this).
    stage->SetTimeCodesPerSecond(1.0);
    if (info.times.empty())
        return;
    const auto [minIt, maxIt] = std::minmax_element(info.times.begin(), info.times.end());
    stage->SetStartTimeCode(*minIt);
    stage->SetEndTimeCode(*maxIt);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[UNRST] authored timeCodes start=%g end=%g samples=%zu tcps=1.0\n", *minIt, *maxIt, info.times.size());
}

static ReadUnrstResult ReadUnrst(const std::string& filePath, const SdfLayer::FileFormatArguments& args)
{
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[UNRST] ReadUnrst('%s') args=%zu\n", filePath.c_str(), args.size());
    for (const auto& [key, value] : args)
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[UNRST]   arg %s='%s'\n", key.c_str(), value.c_str());

    ReadOptions options = ParseReadOptions(filePath, args);
    UnrstInfo info = ParseUnrst(filePath, options.keywordMode, options.timeSource, options.timeScale, options.timeOffset);
    ReadContext ctx = CreateReadContext(std::move(info), std::move(options));

    // Overlay only: define the dataset prim and attach time-sampled fields on
    // it. Grid geometry and ACTNUM are expected from an EGRID or GRDECL layer
    // composed at the same prim path.
    const UsdPrim prim = OmniSciDataset::Define(ctx.stage, ctx.options.rootPath).GetPrim();
    AuthorTimeCodes(ctx.stage, ctx.info);

    for (const TimeField& field : ctx.info.fields)
    {
        AuthorFieldStructure(prim, field);
        RegisterFieldData(ctx, field);
    }

    return { ctx.layer, ctx.fileData };
}

static bool LooksLikeUnrst(const std::string& filePath)
{
    std::string keyword;
    std::string type;
    if (!CaeReadFirstEclipseRecordHeader(filePath, &keyword, &type))
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("[UNRST] LooksLikeUnrst('%s') -> false (no Eclipse header)\n", filePath.c_str());
        return false;
    }
    const bool result = keyword == "SEQNUM" && type == "INTE";
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[UNRST] LooksLikeUnrst('%s') -> %d firstKeyword='%s' type='%s'\n", filePath.c_str(), result ? 1 : 0,
             keyword.c_str(), type.c_str());
    return result;
}

} // namespace unrst_detail

OmniSciUnrstFileFormat::OmniSciUnrstFileFormat()
    : SdfFileFormat(OmniSciUnrstFileFormatTokens->Id,
                    OmniSciUnrstFileFormatTokens->Version,
                    OmniSciUnrstFileFormatTokens->Target,
                    OmniSciUnrstFileFormatTokens->Extension)
{
}

OmniSciUnrstFileFormat::~OmniSciUnrstFileFormat() = default;

bool OmniSciUnrstFileFormat::CanRead(const std::string& filePath) const
{
    const std::string ext = CaeEclipseToLower(TfGetExtension(filePath));
    if (ext != OmniSciUnrstFileFormatTokens->Extension)
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("OmniSciUnrstFileFormat::CanRead('%s') -> false (extension='%s')\n", filePath.c_str(), ext.c_str());
        return false;
    }
    try
    {
        const CaeResolverAssetPtr asset = CaeResolveAsset(filePath);
        const bool result = unrst_detail::LooksLikeUnrst(asset->LocalPath());
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("OmniSciUnrstFileFormat::CanRead('%s') -> %d\n", filePath.c_str(), result ? 1 : 0);
        return result;
    }
    catch (const cae::FileFormatError&)
    {
        return false;
    }
}

bool OmniSciUnrstFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool /*metadataOnly*/) const
{
    if (!TF_VERIFY(layer))
        return false;

    const auto& fmtArgs = layer->GetFileFormatArguments();
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("OmniSciUnrstFileFormat::Read('%s')\n", resolvedPath.c_str());

    try
    {
        const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
        const CaeResolverAssetPtr asset = CaeOpenResolverAsset(identifier, ArResolvedPath(resolvedPath));
        const auto readArgs = CaePrepareResolverArguments(identifier, fmtArgs);
        unrst_detail::ReadUnrstResult result = unrst_detail::ReadUnrst(asset->LocalPath(), readArgs);
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
        TF_RUNTIME_ERROR("OmniSciUnrstFileFormat: %s", ex.what());
        return false;
    }
}

void OmniSciUnrstFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                 const PcpDynamicFileFormatContext& context,
                                                                 FileFormatArguments* args,
                                                                 VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, unrst_detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciUnrstFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, unrst_detail::GetDynamicFileFormatArgs());
}

bool OmniSciUnrstFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciUnrstFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciUnrstFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciUnrstFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
