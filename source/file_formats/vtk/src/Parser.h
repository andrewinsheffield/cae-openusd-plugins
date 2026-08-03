// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file Parser.h
///
/// Metadata-only parsers for legacy VTK and serial VTK XML.

#include "DatasetSpec.h"
#include "Types.h"

#include <memory>
#include <string>

namespace cae::vtk
{

/// Parses one envelope format into immutable DatasetSpec metadata.
class Parser
{
public:
    virtual ~Parser() = default;

    virtual bool CanParse(const std::string& filePath) const = 0;
    virtual DatasetSpec Parse(const std::string& filePath, const ReadOptions& options) const = 0;
};

/// Legacy `.vtk` metadata parser.
class LegacyParser final : public Parser
{
public:
    bool CanParse(const std::string& filePath) const override;

    /// Parse a legacy `.vtk` file into metadata-only dataset specs.
    ///
    /// Binary payloads are recorded as file offsets. ASCII payloads are scanned
    /// only far enough to determine bounded text ranges and value counts; heavy
    /// values are not materialized during parsing.
    DatasetSpec Parse(const std::string& filePath, const ReadOptions& options) const override;
};

/// Serial XML `.vti/.vtr/.vts/.vtp/.vtu` metadata parser.
class XmlParser final : public Parser
{
public:
    bool CanParse(const std::string& filePath) const override;

    /// Parse a serial XML VTK file into metadata-only dataset specs.
    ///
    /// The parser records per-array storage mode and payload locations for
    /// inline ASCII, inline binary, appended raw, and appended base64 arrays.
    /// It validates counts from metadata where possible without decoding heavy
    /// array payloads.
    DatasetSpec Parse(const std::string& filePath, const ReadOptions& options) const override;
};

/// Create the parser that can handle `filePath`, or return null when the file
/// does not look like a supported legacy/serial XML VTK file.
///
/// Parser selection should be content-sniffed instead of relying only on the
/// extension, since extensions can be stale or absent.
std::unique_ptr<Parser> CreateParserForFile(const std::string& filePath);

} // namespace cae::vtk
