// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciGrdeclFileFormat.h"

#include "CaeFileFormatData.h"
#include "ContainerUtils.h"
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
#include <omniSciFileFormatArgs/tokens.h>
#include <omniSciReservoir/cellPropertyAPI.h>
#include <omniSciReservoir/cornerPointGridAPI.h>
#include <omniSciReservoir/tokens.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/gf/vec3i.h>
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
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciGrdeclFileFormatTokens, OMNI_SCI_GRDECL_FILE_FORMAT_TOKENS);

namespace fs = std::filesystem;

namespace grdecl_detail
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 1> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciGrdeclFileFormatTokens->ArgCacheMode },
        } };

    return DynamicFileFormatArgs;
}

struct ReadOptions
{
    SdfPath rootPath;
    CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All;
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

struct Token
{
    std::string text;
    bool quoted = false;
};

static std::string ToUpper(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

static std::string TrimRight(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

static std::string ToLower(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

class DeckTokenizer
{
public:
    explicit DeckTokenizer(std::istream& input) : _input(input)
    {
    }

    bool Next(Token* token)
    {
        if (!token)
            return false;

        char c = '\0';
        if (_skipRestOfLine)
        {
            while (_input.get(c) && c != '\n' && c != '\r')
            {
            }
            _skipRestOfLine = false;
        }

        while (_input.get(c))
        {
            if (std::isspace(static_cast<unsigned char>(c)))
                continue;

            if (c == '-' && _input.peek() == '-')
            {
                _input.get(c);
                while (_input.get(c) && c != '\n' && c != '\r')
                {
                }
                continue;
            }

            break;
        }

        if (!_input)
            return false;

        token->text.clear();
        token->quoted = false;

        if (c == '/')
        {
            token->text = "/";
            _skipRestOfLine = true;
            return true;
        }

        if (c == '\'' || c == '"')
        {
            const char quote = c;
            token->quoted = true;
            while (_input.get(c))
            {
                if (c == quote)
                    break;
                token->text.push_back(c);
            }
            return true;
        }

        token->text.push_back(c);
        while (_input.get(c))
        {
            if (std::isspace(static_cast<unsigned char>(c)))
                break;
            if (c == '-' && _input.peek() == '-')
            {
                _input.unget();
                break;
            }
            if (c == '/')
            {
                _input.unget();
                break;
            }
            token->text.push_back(c);
        }
        return true;
    }

private:
    std::istream& _input;
    bool _skipRestOfLine = false;
};

static bool ParseInt(std::string_view text, int* value)
{
    if (!value)
        return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    int parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end)
        return false;
    *value = parsed;
    return true;
}

static bool ParseSize(std::string_view text, size_t* value)
{
    if (!value)
        return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    size_t parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end)
        return false;
    *value = parsed;
    return true;
}

static bool ParseDouble(const std::string& text, double* value)
{
    if (!value)
        return false;

    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), 'D', 'E');
    std::replace(normalized.begin(), normalized.end(), 'd', 'e');

    char* end = nullptr;
    const double parsed = std::strtod(normalized.c_str(), &end);
    if (end == normalized.c_str() || *end != '\0')
        return false;
    *value = parsed;
    return true;
}

static void ExpandDoubleToken(const std::string& token, std::vector<double>* values)
{
    if (!values)
        return;

    const size_t star = token.find('*');
    if (star == std::string::npos)
    {
        double value = 0.0;
        if (!ParseDouble(token, &value))
            throw cae::FileFormatError("Expected numeric value, got '" + token + "'");
        values->push_back(value);
        return;
    }

    size_t repeat = 0;
    if (!ParseSize(token.substr(0, star), &repeat))
        throw cae::FileFormatError("Expected repeat count in token '" + token + "'");

    double value = 0.0;
    const std::string payload = token.substr(star + 1);
    if (!payload.empty() && !ParseDouble(payload, &value))
        throw cae::FileFormatError("Expected repeated numeric value in token '" + token + "'");
    values->insert(values->end(), repeat, value);
}

static void ExpandIntToken(const std::string& token, std::vector<int>* values)
{
    if (!values)
        return;

    const size_t star = token.find('*');
    if (star == std::string::npos)
    {
        int value = 0;
        if (!ParseInt(token, &value))
            throw cae::FileFormatError("Expected integer value, got '" + token + "'");
        values->push_back(value);
        return;
    }

    size_t repeat = 0;
    if (!ParseSize(token.substr(0, star), &repeat))
        throw cae::FileFormatError("Expected repeat count in token '" + token + "'");

    int value = 0;
    const std::string payload = token.substr(star + 1);
    if (!payload.empty() && !ParseInt(payload, &value))
        throw cae::FileFormatError("Expected repeated integer value in token '" + token + "'");
    values->insert(values->end(), repeat, value);
}

static std::vector<Token> ReadRecordTokens(DeckTokenizer& tokenizer)
{
    std::vector<Token> result;
    Token token;
    while (tokenizer.Next(&token))
    {
        if (token.text == "/")
            break;
        result.push_back(token);
    }
    return result;
}

static void SkipSingleRecord(DeckTokenizer& tokenizer)
{
    Token token;
    while (tokenizer.Next(&token))
    {
        if (token.text == "/")
            return;
    }
}

static void SkipSlashTerminatedBlock(DeckTokenizer& tokenizer)
{
    bool sawValueSinceSlash = false;
    Token token;
    while (tokenizer.Next(&token))
    {
        if (token.text == "/")
        {
            if (!sawValueSinceSlash)
                return;
            sawValueSinceSlash = false;
            continue;
        }
        sawValueSinceSlash = true;
    }
}

static TfToken MakeArrayValueAttrName(const TfToken& arrayName)
{
    return TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

struct ArrayRef
{
    std::string keyword;
    std::string filePath;
    bool integer = false;
    CaeResolverAssetPtr assetLease;
};

struct GridInfo
{
    std::array<int, 3> dims = { 0, 0, 0 };
    bool hasDims = false;
    std::optional<ArrayRef> coord;
    std::optional<ArrayRef> zcorn;
    std::optional<ArrayRef> actnum;
    std::vector<double> mapAxes;
    std::string lengthUnit;
    cae::StringUnorderedMap<ArrayRef> properties;
};

struct ParseState
{
    GridInfo grid;
    cae::StringSet visitedFiles;
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

using ReadGrdeclResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;

static bool IsDeckStopKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "SOLUTION",
        "SUMMARY",
        "SCHEDULE",
        "END",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsNoDataKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "RUNSPEC", "GRID",   "EDIT",    "PROPS",   "REGIONS",  "OIL",       "WATER", "GAS",
        "DISGAS",  "VAPOIL", "METRIC",  "FIELD",   "FIELD_US", "FIELD_USA", "LAB",   "INIT",
        "NOECHO",  "ECHO",   "NEWTRAN", "OLDTRAN", "UNIFIN",   "UNIFOUT",   "NOSIM",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsBlockKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "ADD", "COPY", "EQUALS", "MULTIPLY", "FAULTS", "MULTFLT", "MULTREGT",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsDoubleCellPropertyKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "PORO", "PERMX", "PERMY", "PERMZ", "NTG",   "SWATINIT", "SWCR",  "SWL",
        "SWU",  "SGL",   "SGCR",  "SGU",   "SOGCR", "SOWCR",    "ISGCR",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsIntCellPropertyKeyword(const std::string& keyword)
{
    static const cae::StringUnorderedSet keywords = {
        "EQLNUM", "FIPNUM", "FLUXNUM", "SATNUM", "IMBNUM", "PVTNUM", "ROCKNUM",
    };
    return cae::Contains(keywords, keyword);
}

static bool IsSupportedCellPropertyKeyword(const std::string& keyword, bool* integer)
{
    if (IsIntCellPropertyKeyword(keyword))
    {
        if (integer)
            *integer = true;
        return true;
    }
    if (IsDoubleCellPropertyKeyword(keyword))
    {
        if (integer)
            *integer = false;
        return true;
    }
    return false;
}

static void ParseDimRecord(const std::vector<Token>& record, GridInfo* grid)
{
    if (!grid || record.size() < 3)
        return;

    int nx = 0;
    int ny = 0;
    int nz = 0;
    if (!ParseInt(record[0].text, &nx) || !ParseInt(record[1].text, &ny) || !ParseInt(record[2].text, &nz))
        throw cae::FileFormatError("DIMENS/SPECGRID record does not start with three integer dimensions.");
    if (nx <= 0 || ny <= 0 || nz <= 0)
        throw cae::FileFormatError("DIMENS/SPECGRID dimensions must be positive.");

    grid->dims = { nx, ny, nz };
    grid->hasDims = true;
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] parsed dims=(%d,%d,%d)\n", nx, ny, nz);
}

static void ParseMapAxesRecord(const std::vector<Token>& record, GridInfo* grid)
{
    if (!grid)
        return;
    std::vector<double> values;
    values.reserve(6);
    for (const Token& token : record)
        ExpandDoubleToken(token.text, &values);
    if (values.size() >= 6)
    {
        grid->mapAxes.assign(values.begin(), values.begin() + 6);
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] parsed MAPAXES\n");
    }
}

static void ParseGridUnitRecord(const std::vector<Token>& record, GridInfo* grid)
{
    if (!grid || record.empty())
        return;
    const std::string unitText = TrimRight(record.front().text);
    const std::string unit = ToUpper(unitText);
    if (unit == "METRES" || unit == "METERS" || unit == "METRIC" || unit == "M")
        grid->lengthUnit = "m";
    else if (unit == "FEET" || unit == "FIELD" || unit == "FT")
        grid->lengthUnit = "ft";
    else if (unit == "CM" || unit == "CENTIMETRES" || unit == "CENTIMETERS")
        grid->lengthUnit = "cm";
    else
        grid->lengthUnit = unitText;
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[GRDECL] parsed length unit source='%s' normalized='%s'\n", unitText.c_str(), grid->lengthUnit.c_str());
}

static void ParseDeckFile(const CaeResolverAssetPtr& asset, ParseState* state, int depth)
{
    if (!state)
        return;
    if (depth > 64)
        throw cae::FileFormatError("GRDECL include nesting is too deep near " + asset->Identifier());

    const std::string& visitPath = asset->Identifier();
    if (!state->visitedFiles.insert(visitPath).second)
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] skipping already-visited include '%s'\n", visitPath.c_str());
        return;
    }

    std::ifstream input(asset->LocalPath(), std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open GRDECL/DATA file: " + asset->Identifier());
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] parsing deck file '%s' depth=%d\n", asset->Identifier().c_str(), depth);

    DeckTokenizer tokenizer(input);
    Token token;
    while (tokenizer.Next(&token))
    {
        if (token.text == "/")
            continue;

        const std::string keyword = ToUpper(token.text);

        if (IsDeckStopKeyword(keyword))
        {
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[GRDECL] stop keyword '%s' reached in '%s'\n", keyword.c_str(), visitPath.c_str());
            break;
        }

        if (keyword == "METRIC")
        {
            state->grid.lengthUnit = "m";
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] METRIC keyword -> lengthUnit=m\n");
            continue;
        }
        if (keyword == "FIELD" || keyword == "FIELD_US" || keyword == "FIELD_USA")
        {
            state->grid.lengthUnit = "ft";
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] FIELD keyword -> lengthUnit=ft\n");
            continue;
        }

        if (IsNoDataKeyword(keyword))
            continue;

        if (keyword == "INCLUDE")
        {
            const std::vector<Token> record = ReadRecordTokens(tokenizer);
            if (!record.empty())
            {
                CaeResolverAssetPtr includeAsset = CaeResolveSiblingAsset(asset->Identifier(), record.front().text);
                TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                    .Msg("[GRDECL] include '%s' resolved to '%s'\n", record.front().text.c_str(),
                         includeAsset->Identifier().c_str());
                ParseDeckFile(includeAsset, state, depth + 1);
            }
            continue;
        }

        if (keyword == "DIMENS" || keyword == "SPECGRID")
        {
            ParseDimRecord(ReadRecordTokens(tokenizer), &state->grid);
            continue;
        }

        if (keyword == "MAPAXES")
        {
            ParseMapAxesRecord(ReadRecordTokens(tokenizer), &state->grid);
            continue;
        }

        if (keyword == "GRIDUNIT" || keyword == "MAPUNITS")
        {
            ParseGridUnitRecord(ReadRecordTokens(tokenizer), &state->grid);
            continue;
        }

        if (keyword == "COORD")
        {
            state->grid.coord = ArrayRef{ keyword, asset->LocalPath(), false, asset };
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] COORD stored in '%s'\n", visitPath.c_str());
            SkipSingleRecord(tokenizer);
            continue;
        }

        if (keyword == "ZCORN")
        {
            state->grid.zcorn = ArrayRef{ keyword, asset->LocalPath(), false, asset };
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] ZCORN stored in '%s'\n", visitPath.c_str());
            SkipSingleRecord(tokenizer);
            continue;
        }

        if (keyword == "ACTNUM")
        {
            state->grid.actnum = ArrayRef{ keyword, asset->LocalPath(), true, asset };
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] ACTNUM stored in '%s'\n", visitPath.c_str());
            SkipSingleRecord(tokenizer);
            continue;
        }

        bool integerProperty = false;
        if (IsSupportedCellPropertyKeyword(keyword, &integerProperty))
        {
            state->grid.properties[keyword] = ArrayRef{ keyword, asset->LocalPath(), integerProperty, asset };
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[GRDECL] selected property '%s' type=%s file='%s'\n", keyword.c_str(),
                     integerProperty ? "int" : "double", visitPath.c_str());
            SkipSingleRecord(tokenizer);
            continue;
        }

        if (IsBlockKeyword(keyword))
        {
            SkipSlashTerminatedBlock(tokenizer);
            continue;
        }

        SkipSingleRecord(tokenizer);
    }
}

static GridInfo ParseDeck(const CaeResolverAssetPtr& asset)
{
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] ParseDeck('%s')\n", asset->Identifier().c_str());
    ParseState state;
    ParseDeckFile(asset, &state, 0);
    if (!state.grid.hasDims)
        throw cae::FileFormatError("GRDECL reader did not find DIMENS or SPECGRID.");
    if (!state.grid.coord)
        throw cae::FileFormatError("GRDECL reader did not find COORD.");
    if (!state.grid.zcorn)
        throw cae::FileFormatError("GRDECL reader did not find ZCORN.");
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[GRDECL] deck summary dims=(%d,%d,%d) hasACTNUM=%d properties=%zu visitedFiles=%zu lengthUnit='%s' mapAxes=%zu\n",
             state.grid.dims[0], state.grid.dims[1], state.grid.dims[2], state.grid.actnum ? 1 : 0,
             state.grid.properties.size(), state.visitedFiles.size(), state.grid.lengthUnit.c_str(),
             state.grid.mapAxes.size());
    return state.grid;
}

static VtDoubleArray ToVtDoubleArray(std::vector<double> values)
{
    UninitializedVtArray<double> result = MakeUninitializedVtArray<double>(values.size());
    if (!values.empty())
        std::copy(values.begin(), values.end(), result.data);
    return std::move(result.array);
}

static VtIntArray ToVtIntArray(std::vector<int> values)
{
    UninitializedVtArray<int> result = MakeUninitializedVtArray<int>(values.size());
    if (!values.empty())
        std::copy(values.begin(), values.end(), result.data);
    return std::move(result.array);
}

static VtIntArray BuildLogicalCellToActiveCellArray(const VtIntArray& actnum)
{
    if (actnum.empty())
        return {};

    UninitializedVtArray<int> result = MakeUninitializedVtArray<int>(actnum.size());
    int* mapping = result.data;
    if (!mapping)
        throw std::logic_error("Uninitialized GRDECL cell map storage is missing");

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

static void CheckExpectedCount(const std::string& keyword, size_t actual, size_t expected)
{
    if (expected != 0 && actual != expected)
    {
        throw cae::FileFormatError(keyword + " expected " + std::to_string(expected) + " values, read " +
                                   std::to_string(actual));
    }
}

static VtDoubleArray LoadDoubleKeywordArray(const ArrayRef& ref, size_t expectedCount)
{
    std::ifstream input(ref.filePath, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open GRDECL array file: " + ref.filePath);

    DeckTokenizer tokenizer(input);
    Token token;
    while (tokenizer.Next(&token))
    {
        if (ToUpper(token.text) != ref.keyword)
            continue;

        std::vector<double> values;
        if (expectedCount != 0)
            values.reserve(expectedCount);
        while (tokenizer.Next(&token))
        {
            if (token.text == "/")
                break;
            ExpandDoubleToken(token.text, &values);
        }
        CheckExpectedCount(ref.keyword, values.size(), expectedCount);
        return ToVtDoubleArray(std::move(values));
    }

    throw cae::FileFormatError("Keyword " + ref.keyword + " not found in " + ref.filePath);
}

static VtIntArray LoadIntKeywordArray(const ArrayRef& ref, size_t expectedCount)
{
    std::ifstream input(ref.filePath, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open GRDECL array file: " + ref.filePath);

    DeckTokenizer tokenizer(input);
    Token token;
    while (tokenizer.Next(&token))
    {
        if (ToUpper(token.text) != ref.keyword)
            continue;

        std::vector<int> values;
        if (expectedCount != 0)
            values.reserve(expectedCount);
        while (tokenizer.Next(&token))
        {
            if (token.text == "/")
                break;
            ExpandIntToken(token.text, &values);
        }
        CheckExpectedCount(ref.keyword, values.size(), expectedCount);
        return ToVtIntArray(std::move(values));
    }

    throw cae::FileFormatError("Keyword " + ref.keyword + " not found in " + ref.filePath);
}

static VtIntArray LoadLogicalCellToActiveCellArray(const ArrayRef& actnum, size_t expectedCount)
{
    return BuildLogicalCellToActiveCellArray(LoadIntKeywordArray(actnum, expectedCount));
}

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

static CaeFileFormatData::Loader MakeDoubleLoader(ArrayRef ref, size_t expectedCount)
{
    return [ref = std::move(ref), expectedCount]()
    {
        VtDoubleArray values = LoadDoubleKeywordArray(ref, expectedCount);
        return VtValue::Take(values);
    };
}

static CaeFileFormatData::Loader MakeIntLoader(ArrayRef ref, size_t expectedCount)
{
    return [ref = std::move(ref), expectedCount]()
    {
        VtIntArray values = LoadIntKeywordArray(ref, expectedCount);
        return VtValue::Take(values);
    };
}

static CaeFileFormatData::Loader MakeLogicalCellToActiveCellLoader(ArrayRef ref, size_t expectedCount)
{
    return [ref = std::move(ref), expectedCount]()
    {
        VtIntArray values = LoadLogicalCellToActiveCellArray(ref, expectedCount);
        return VtValue::Take(values);
    };
}

static ReadOptions ParseReadOptions(const std::string& filePath, const SdfLayer::FileFormatArguments& args)
{
    ReadOptions options;
    options.rootPath = CaeResolveRootPrimPath(filePath, args);
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[GRDECL] options root=%s cacheMode=%s\n", options.rootPath.GetText(), CacheModeName(options.cacheMode));
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

static void RegisterGridArray(
    ReadContext& ctx, UsdPrim prim, const TfToken& arrayName, const TfToken& valueType, CaeFileFormatData::Loader loader)
{
    ApplyArrayAPI(prim, arrayName);
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[GRDECL] register grid array %s type=%s root=%s\n", arrayName.GetText(), valueType.GetText(),
             ctx.options.rootPath.GetText());
    ctx.fileData->RegisterLazySingleState(
        ctx.options.rootPath, MakeArrayValueAttrName(arrayName), valueType, 0.0, std::move(loader));
}

static ReadGrdeclResult ReadGrdecl(const CaeResolverAssetPtr& asset, const SdfLayer::FileFormatArguments& args)
{
    const std::string& filePath = asset->LocalPath();
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL] ReadGrdecl('%s') args=%zu\n", filePath.c_str(), args.size());
    for (const auto& [key, value] : args)
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("[GRDECL]   arg %s='%s'\n", key.c_str(), value.c_str());

    GridInfo grid = ParseDeck(asset);
    ReadContext ctx = CreateReadContext(std::move(grid), ParseReadOptions(filePath, args));

    const UsdPrim gridPrim = OmniSciDataset::Define(ctx.stage, ctx.options.rootPath).GetPrim();
    OmniSciReservoirCornerPointGridAPI gridAPI = OmniSciReservoirCornerPointGridAPI::Apply(gridPrim);
    gridAPI.CreateLogicalCellDimsAttr().Set(GfVec3i(ctx.grid.dims[0], ctx.grid.dims[1], ctx.grid.dims[2]));
    gridAPI.CreateSourceFormatAttr().Set(OmniSciReservoirTokens->grdecl);
    gridAPI.CreateIndexOrderAttr().Set(OmniSciReservoirTokens->eclipse);
    gridAPI.CreateDepthDirectionAttr().Set(OmniSciReservoirTokens->zDown);
    gridAPI.CreateNameAttr().Set(TfStringGetBeforeSuffix(TfGetBaseName(filePath)));
    if (!ctx.grid.lengthUnit.empty())
        gridAPI.CreateLengthUnitAttr().Set(ctx.grid.lengthUnit);
    if (!ctx.grid.mapAxes.empty())
        gridAPI.CreateMapAxesAttr().Set(ToVtDoubleArray(ctx.grid.mapAxes));

    if (ctx.grid.coord)
    {
        RegisterGridArray(ctx, gridPrim, OmniSciReservoirTokens->coord, TfToken("double[]"),
                          MakeDoubleLoader(*ctx.grid.coord, CoordValueCount(ctx.grid)));
    }
    if (ctx.grid.zcorn)
    {
        RegisterGridArray(ctx, gridPrim, OmniSciReservoirTokens->zcorn, TfToken("double[]"),
                          MakeDoubleLoader(*ctx.grid.zcorn, ZcornValueCount(ctx.grid)));
    }
    if (ctx.grid.actnum)
    {
        RegisterGridArray(ctx, gridPrim, OmniSciReservoirTokens->actnum, TfToken("int[]"),
                          MakeIntLoader(*ctx.grid.actnum, NumLogicalCells(ctx.grid)));
        RegisterGridArray(ctx, gridPrim, OmniSciReservoirTokens->logicalCellToActiveCell, TfToken("int[]"),
                          MakeLogicalCellToActiveCellLoader(*ctx.grid.actnum, NumLogicalCells(ctx.grid)));
    }

    for (const auto& [keyword, property] : ctx.grid.properties)
    {
        const TfToken instanceName(TfMakeValidIdentifier(keyword));
        OmniSciFieldAPI fieldAPI = OmniSciFieldAPI::Apply(gridPrim, instanceName);
        fieldAPI.CreateNameAttr().Set(property.keyword);
        fieldAPI.CreateAssociationAttr().Set(OmniSciTokens->element);
        ApplyArrayAPI(gridPrim, instanceName);
        OmniSciReservoirCellPropertyAPI propertyAPI = OmniSciReservoirCellPropertyAPI::Apply(gridPrim, instanceName);
        propertyAPI.CreateIndexSpaceAttr().Set(OmniSciReservoirTokens->logicalCells);
        propertyAPI.CreateSourceKeywordAttr().Set(property.keyword);

        if (property.integer)
        {
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[GRDECL] register int property '%s' values=%zu\n", property.keyword.c_str(),
                     NumLogicalCells(ctx.grid));
            ctx.fileData->RegisterLazySingleState(ctx.options.rootPath, MakeArrayValueAttrName(instanceName),
                                                  TfToken("int[]"), 0.0,
                                                  MakeIntLoader(property, NumLogicalCells(ctx.grid)));
        }
        else
        {
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[GRDECL] register double property '%s' values=%zu\n", property.keyword.c_str(),
                     NumLogicalCells(ctx.grid));
            ctx.fileData->RegisterLazySingleState(ctx.options.rootPath, MakeArrayValueAttrName(instanceName),
                                                  TfToken("double[]"), 0.0,
                                                  MakeDoubleLoader(property, NumLogicalCells(ctx.grid)));
        }
    }

    return { ctx.layer, ctx.fileData };
}

static bool LooksLikeGrdecl(const std::string& filePath)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        return false;

    DeckTokenizer tokenizer(input);
    Token token;
    size_t count = 0;
    bool sawDeckMarker = false;
    while (count++ < 4096 && tokenizer.Next(&token))
    {
        const std::string keyword = ToUpper(token.text);
        if (keyword == "COORD" || keyword == "ZCORN" || keyword == "SPECGRID")
        {
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[GRDECL] LooksLikeGrdecl('%s') -> true (keyword=%s)\n", filePath.c_str(), keyword.c_str());
            return true;
        }
        if (keyword == "DIMENS" || keyword == "RUNSPEC" || keyword == "GRID")
            sawDeckMarker = true;
        if (sawDeckMarker && keyword == "INCLUDE")
        {
            TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
                .Msg("[GRDECL] LooksLikeGrdecl('%s') -> true (deck marker + INCLUDE)\n", filePath.c_str());
            return true;
        }
    }

    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
        .Msg("[GRDECL] LooksLikeGrdecl('%s') -> %d (sawDeckMarker=%d)\n", filePath.c_str(), sawDeckMarker ? 1 : 0,
             sawDeckMarker ? 1 : 0);
    return sawDeckMarker;
}

} // namespace grdecl_detail

OmniSciGrdeclFileFormat::OmniSciGrdeclFileFormat()
    : SdfFileFormat(OmniSciGrdeclFileFormatTokens->Id,
                    OmniSciGrdeclFileFormatTokens->Version,
                    OmniSciGrdeclFileFormatTokens->Target,
                    OmniSciGrdeclFileFormatTokens->Extension)
{
}

OmniSciGrdeclFileFormat::~OmniSciGrdeclFileFormat() = default;

bool OmniSciGrdeclFileFormat::CanRead(const std::string& filePath) const
{
    const std::string ext = grdecl_detail::ToLower(TfGetExtension(filePath));
    if (ext != OmniSciGrdeclFileFormatTokens->Extension && ext != OmniSciGrdeclFileFormatTokens->AliasExtension)
    {
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("OmniSciGrdeclFileFormat::CanRead('%s') -> false (extension='%s')\n", filePath.c_str(), ext.c_str());
        return false;
    }
    try
    {
        const CaeResolverAssetPtr asset = CaeResolveAsset(filePath);
        const bool result = grdecl_detail::LooksLikeGrdecl(asset->LocalPath());
        TF_DEBUG(CAE_ECLIPSE_FILEFORMAT)
            .Msg("OmniSciGrdeclFileFormat::CanRead('%s') -> %d\n", filePath.c_str(), result ? 1 : 0);
        return result;
    }
    catch (const cae::FileFormatError&)
    {
        return false;
    }
}

bool OmniSciGrdeclFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool /*metadataOnly*/) const
{
    if (!TF_VERIFY(layer))
        return false;

    const auto& fmtArgs = layer->GetFileFormatArguments();
    TF_DEBUG(CAE_ECLIPSE_FILEFORMAT).Msg("OmniSciGrdeclFileFormat::Read('%s')\n", resolvedPath.c_str());

    try
    {
        const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
        const CaeResolverAssetPtr asset = CaeOpenResolverAsset(identifier, ArResolvedPath(resolvedPath));
        const auto readArgs = CaePrepareResolverArguments(identifier, fmtArgs);
        grdecl_detail::ReadGrdeclResult result = grdecl_detail::ReadGrdecl(asset, readArgs);
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
        TF_RUNTIME_ERROR("OmniSciGrdeclFileFormat: %s", ex.what());
        return false;
    }
}

void OmniSciGrdeclFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                  const PcpDynamicFileFormatContext& context,
                                                                  FileFormatArguments* args,
                                                                  VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, grdecl_detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciGrdeclFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, grdecl_detail::GetDynamicFileFormatArgs());
}

bool OmniSciGrdeclFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciGrdeclFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciGrdeclFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciGrdeclFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
