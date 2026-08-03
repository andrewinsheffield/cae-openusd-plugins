// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DisablePXRWarnings.h"
#include "FileFormatError.h"
#include "OmniSciFileFormatSharedAPI.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/pxr.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/sdf/fileFormat.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <memory>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class ArAsset;
class SdfLayer;
struct CaeResolverAssetFactory;

/// Owns resolver access to one asset and exposes a lease-scoped local path.
///
/// OpenUSD resolver paths are opaque and may remain URLs. Readers backed by
/// filename-only libraries therefore cannot use ArResolvedPath directly.
/// CaeResolverAsset opens the resolved asset through ArResolver and provides a
/// native path suitable for those libraries.
///
/// LocalPath() is valid only while this object remains alive. It may name the
/// original file, a resolver-managed cache entry, or a temporary materialized
/// copy. Callers must not persist it in USD, use it as a composition
/// identifier, or assume that adjacent files are available beside it.
///
/// The object is immutable after construction and may be shared by deferred
/// loaders. Use CaeFileFormatData::KeepAlive() when a lazy loader captures
/// LocalPath().
class OMNI_SCI_FILE_FORMAT_SHARED_TYPE CaeResolverAsset
{
public:
    /// Releases the ArAsset and removes any temporary materialized copy.
    OMNI_SCI_FILE_FORMAT_SHARED_API ~CaeResolverAsset();

    CaeResolverAsset(const CaeResolverAsset&) = delete;
    CaeResolverAsset& operator=(const CaeResolverAsset&) = delete;

    /// Returns the original asset identifier without file-format arguments.
    ///
    /// This is the stable value to use for diagnostics, root naming,
    /// composition, and resolving explicitly named sibling assets.
    const std::string& Identifier() const
    {
        return _identifier;
    }

    /// Returns the opaque path produced by ArResolver::Resolve().
    const ArResolvedPath& ResolvedPath() const
    {
        return _resolvedPath;
    }

    /// Returns a native path accepted by filename-only reader libraries.
    ///
    /// The path is lease-scoped and must not outlive this object.
    const std::string& LocalPath() const
    {
        return _localPath;
    }

private:
    friend struct CaeResolverAssetFactory;

    CaeResolverAsset(std::string identifier,
                     ArResolvedPath resolvedPath,
                     std::shared_ptr<ArAsset> asset,
                     std::string localPath,
                     std::string temporaryDirectory);

    std::string _identifier;
    ArResolvedPath _resolvedPath;
    std::shared_ptr<ArAsset> _asset;
    std::string _localPath;
    std::string _temporaryDirectory;
};

using CaeResolverAssetPtr = std::shared_ptr<CaeResolverAsset>;

/// Opens an already-resolved asset and creates a local-path lease.
///
/// Prefer this overload from SdfFileFormat::Read(), which already receives the
/// resolved path from OpenUSD. The function uses a native resolved file
/// directly, otherwise opens ArAsset, reuses its native backing file when
/// possible, and finally falls back to materializing ArAsset::Read() into
/// temporary storage.
///
/// @param identifier The original identifier used to create the layer.
/// @param resolvedPath The opaque path supplied by the active resolver.
/// @throws cae::FileFormatError if the asset cannot be opened or materialized.
OMNI_SCI_FILE_FORMAT_SHARED_API CaeResolverAssetPtr CaeOpenResolverAsset(const std::string& identifier,
                                                                         const ArResolvedPath& resolvedPath);

/// Resolves and opens an asset identifier through the active ArResolver.
///
/// This is primarily intended for CanRead() and other call sites that have not
/// already received an ArResolvedPath.
///
/// @throws cae::FileFormatError if resolution, opening, or materialization fails.
OMNI_SCI_FILE_FORMAT_SHARED_API CaeResolverAssetPtr CaeResolveAsset(const std::string& identifier);

/// Resolves an explicitly named child asset relative to an anchor asset.
///
/// The function resolves @p anchorIdentifier, calls
/// ArResolver::CreateIdentifier() for @p referencedPath, then opens the child
/// through the same resolver. It does not scan directories or expand
/// wildcards. Retain the returned lease for as long as the child local path is
/// used.
///
/// @throws cae::FileFormatError if the anchor or child cannot be resolved,
/// opened, or materialized.
OMNI_SCI_FILE_FORMAT_SHARED_API CaeResolverAssetPtr CaeResolveSiblingAsset(const std::string& anchorIdentifier,
                                                                           const std::string& referencedPath);

/// Returns a layer's original identifier with Sdf file-format arguments removed.
OMNI_SCI_FILE_FORMAT_SHARED_API std::string CaeGetLayerAssetIdentifier(const SdfLayer& layer);

/// Preserves identifier-derived root naming when native I/O uses a cache path.
///
/// If @p args does not contain mountPath, this returns a copy with mountPath
/// derived from @p identifier. Filename-oriented readers should pass the
/// returned arguments to their structure and heavy-data passes.
OMNI_SCI_FILE_FORMAT_SHARED_API SdfFileFormat::FileFormatArguments CaePrepareResolverArguments(
    const std::string& identifier, const SdfFileFormat::FileFormatArguments& args);

/// Returns true when adjacent-directory scanning is safe and meaningful.
///
/// Both the original identifier and resolved path must represent a native
/// filesystem file. A resolver-backed cache path is intentionally insufficient
/// because its neighboring files do not represent the source dataset layout.
OMNI_SCI_FILE_FORMAT_SHARED_API bool CaeCanScanAdjacentFiles(const std::string& identifier,
                                                             const ArResolvedPath& resolvedPath);

/// Rejects a resolver-backed layout that requires directory enumeration.
///
/// Directory-oriented readers should call this at the start of Read() and
/// translate the exception into a TF_RUNTIME_ERROR naming their format.
///
/// @throws cae::FileFormatError with an actionable unsupported-layout message.
OMNI_SCI_FILE_FORMAT_SHARED_API void CaeRequireAdjacentFileScanning(const char* formatName,
                                                                    const std::string& identifier,
                                                                    const ArResolvedPath& resolvedPath);

PXR_NAMESPACE_CLOSE_SCOPE
