// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DisablePXRWarnings.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>
#include <pxr/usd/pcp/dynamicFileFormatContext.h>
#include <pxr/usd/sdf/fileFormat.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

struct CaeDynamicFileFormatArg
{
    TfToken attrName;
    TfToken argName;
};

inline bool CaeDynamicFileFormatValueToString(const VtValue& value, std::string* out)
{
    if (!out || value.IsEmpty())
        return false;

    if (value.IsHolding<std::string>())
    {
        *out = value.UncheckedGet<std::string>();
        return true;
    }
    if (value.IsHolding<VtArray<std::string>>())
    {
        const VtArray<std::string>& values = value.UncheckedGet<VtArray<std::string>>();
        out->clear();
        for (size_t cc = 0; cc < values.size(); ++cc)
        {
            if (cc > 0)
                *out += ",";
            *out += values[cc];
        }
        return true;
    }
    if (value.IsHolding<TfToken>())
    {
        *out = value.UncheckedGet<TfToken>().GetString();
        return true;
    }
    if (value.IsHolding<bool>())
    {
        *out = value.UncheckedGet<bool>() ? "true" : "false";
        return true;
    }
    if (value.IsHolding<int>())
    {
        *out = TfStringify(value.UncheckedGet<int>());
        return true;
    }
    if (value.IsHolding<int64_t>())
    {
        *out = TfStringify(value.UncheckedGet<int64_t>());
        return true;
    }
    if (value.IsHolding<float>())
    {
        *out = TfStringify(value.UncheckedGet<float>());
        return true;
    }
    if (value.IsHolding<double>())
    {
        *out = TfStringify(value.UncheckedGet<double>());
        return true;
    }

    return false;
}

inline void CaeComposeDynamicFileFormatArguments(const PcpDynamicFileFormatContext& context,
                                                 const CaeDynamicFileFormatArg* argSpecs,
                                                 size_t argSpecCount,
                                                 SdfFileFormat::FileFormatArguments* args)
{
    if (!args || !argSpecs)
        return;

    VtValue value;
    for (size_t cc = 0; cc < argSpecCount; ++cc)
    {
        value = VtValue();
        if (!context.ComposeAttributeDefaultValue(argSpecs[cc].attrName, &value))
            continue;

        std::string argValue;
        if (CaeDynamicFileFormatValueToString(value, &argValue))
            (*args)[argSpecs[cc].argName.GetString()] = argValue;
    }
}

template <size_t N>
inline void CaeComposeDynamicFileFormatArguments(const PcpDynamicFileFormatContext& context,
                                                 const CaeDynamicFileFormatArg (&argSpecs)[N],
                                                 SdfFileFormat::FileFormatArguments* args)
{
    CaeComposeDynamicFileFormatArguments(context, argSpecs, N, args);
}

template <size_t N>
inline void CaeComposeDynamicFileFormatArguments(const PcpDynamicFileFormatContext& context,
                                                 const std::array<CaeDynamicFileFormatArg, N>& argSpecs,
                                                 SdfFileFormat::FileFormatArguments* args)
{
    CaeComposeDynamicFileFormatArguments(context, argSpecs.data(), argSpecs.size(), args);
}

inline bool CaeCanDynamicFileFormatAttributeChangeAffectArguments(const TfToken& attributeName,
                                                                  const VtValue& oldValue,
                                                                  const VtValue& newValue,
                                                                  const CaeDynamicFileFormatArg* argSpecs,
                                                                  size_t argSpecCount)
{
    if (!argSpecs)
        return false;

    bool tracksAttribute = false;
    for (size_t cc = 0; cc < argSpecCount; ++cc)
    {
        if (argSpecs[cc].attrName == attributeName)
        {
            tracksAttribute = true;
            break;
        }
    }

    if (!tracksAttribute)
        return false;

    std::string oldArg;
    std::string newArg;
    const bool oldConverts = CaeDynamicFileFormatValueToString(oldValue, &oldArg);
    const bool newConverts = CaeDynamicFileFormatValueToString(newValue, &newArg);

    if (oldConverts != newConverts)
        return true;
    if (!oldConverts)
        return false;
    return oldArg != newArg;
}

template <size_t N>
inline bool CaeCanDynamicFileFormatAttributeChangeAffectArguments(const TfToken& attributeName,
                                                                  const VtValue& oldValue,
                                                                  const VtValue& newValue,
                                                                  const CaeDynamicFileFormatArg (&argSpecs)[N])
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(attributeName, oldValue, newValue, argSpecs, N);
}

template <size_t N>
inline bool CaeCanDynamicFileFormatAttributeChangeAffectArguments(const TfToken& attributeName,
                                                                  const VtValue& oldValue,
                                                                  const VtValue& newValue,
                                                                  const std::array<CaeDynamicFileFormatArg, N>& argSpecs)
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, argSpecs.data(), argSpecs.size());
}

PXR_NAMESPACE_CLOSE_SCOPE
