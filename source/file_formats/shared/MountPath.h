// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Header-only helpers for the shared root-placement contract: every
// file-format plugin's layer default prim is derived from the filename stem;
// an optional "mountPath" SdfFileFormat::FileFormatArguments arg lets a
// sublayer caller relocate the content under a host-supplied path by authoring
// `over` primSpecs at every ancestor and setting the default prim to the
// topmost component.
//
// "mountPath" is intentionally NOT part of the omniSciFileFormatArgs schema and
// NOT in any plugin's CaeDynamicFileFormatArg[] table -- it cannot be authored
// as payload-attribute sugar; it only flows through flat layer-identifier args
// in sublayer composition.

#pragma once

#include "DisablePXRWarnings.h"
#include "FileFormatError.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/primSpec.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <stdexcept>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

inline const std::string CaeMountPathArgumentName("mountPath");

// Stable name of the sublayer-only mount-path argument.
inline const std::string& CaeMountPathArgName()
{
    return CaeMountPathArgumentName;
}

// Resolve where the plugin should author its typed root prim.
// - mountPath unset: returns /<TfMakeValidIdentifier(stem(filePath))>, where
//   the stem is the basename with its last extension stripped. If the resulting
//   identifier is empty, "asset" is used as a fallback.
// - mountPath set: returns the parsed leaf path after validation.
// Throws std::runtime_error when mountPath is malformed (not absolute, not a
// prim path, contains a property suffix, or contains a variant selection).
inline SdfPath CaeResolveRootPrimPath(const std::string& filePath, const SdfFileFormat::FileFormatArguments& args)
{
    const auto it = args.find(CaeMountPathArgName());
    if (it == args.end() || it->second.empty())
    {
        std::string stem = TfStringGetBeforeSuffix(TfGetBaseName(filePath));
        if (stem.empty())
            stem = TfGetBaseName(filePath);
        std::string ident = TfMakeValidIdentifier(stem);
        if (ident.empty())
            ident = "asset";
        return SdfPath::AbsoluteRootPath().AppendChild(TfToken(ident));
    }

    const std::string& raw = it->second;
    if (raw.empty() || raw[0] != '/')
        throw cae::FileFormatError("mountPath must be an absolute SdfPath: '" + raw + "'");

    const SdfPath path(raw);
    if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
        throw cae::FileFormatError("mountPath must name at least one prim component: '" + raw + "'");

    if (!path.IsPrimPath())
        throw cae::FileFormatError("mountPath must be a prim path (no property suffix, no targets): '" + raw + "'");

    if (path.ContainsPrimVariantSelection())
        throw cae::FileFormatError("mountPath must not contain variant selections: '" + raw + "'");

    return path;
}

// Author `over` primSpecs for every ancestor of `leafPath`, and set the layer's
// default prim to the topmost component of `leafPath`. No-op (other than the
// SetDefaultPrim) when `leafPath` has a single component.
//
// Plugins call this AFTER TransferContent / population is complete so the leaf
// already exists as a `def` primSpec authored by the plugin. Note that
// UsdStage::DefinePrim creates implicit ancestor primSpecs as typeless `def`s,
// so this helper flips each ancestor's specifier from `def` to `over` (and
// creates any missing ancestor as `over`) so the layer composes cleanly when
// sublayered into a host stage that already defines those prims.
inline void CaeAuthorMountPathOvers(SdfLayer* layer, const SdfPath& leafPath)
{
    if (!layer || leafPath.IsEmpty() || !leafPath.IsAbsolutePath() || !leafPath.IsPrimPath())
        return;

    const auto prefixes = leafPath.GetPrefixes();
    if (prefixes.empty())
        return;

    layer->SetDefaultPrim(prefixes.front().GetNameToken());

    if (prefixes.size() <= 1)
        return;

    for (size_t cc = 0; cc + 1 < prefixes.size(); ++cc)
    {
        SdfPrimSpecHandle spec = layer->GetPrimAtPath(prefixes[cc]);
        if (!spec)
            spec = SdfCreatePrimInLayer(SdfLayerHandle(layer), prefixes[cc]);
        if (spec)
            spec->SetSpecifier(SdfSpecifierOver);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
