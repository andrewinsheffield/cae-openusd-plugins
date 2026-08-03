// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Parser.h"

#include <memory>

namespace cae::vtk
{

std::unique_ptr<Parser> CreateParserForFile(const std::string& filePath)
{
    auto legacy = std::make_unique<LegacyParser>();
    if (legacy->CanParse(filePath))
        return legacy;

    auto xml = std::make_unique<XmlParser>();
    if (xml->CanParse(filePath))
        return xml;

    return nullptr;
}

} // namespace cae::vtk
