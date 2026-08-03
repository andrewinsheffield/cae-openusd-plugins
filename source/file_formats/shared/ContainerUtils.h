// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cae
{

struct TransparentStringHash
{
    using is_transparent = void;

    size_t operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }
};

template <typename Value>
using StringMap = std::map<std::string, Value, std::less<>>;

using StringSet = std::set<std::string, std::less<>>;

template <typename Value>
using StringUnorderedMap = std::unordered_map<std::string, Value, TransparentStringHash, std::equal_to<>>;

using StringUnorderedSet = std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>;

template <typename Container, typename Key>
bool Contains(const Container& container, const Key& key)
{
    return container.find(key) != container.end();
}

inline bool StringContains(std::string_view text, std::string_view value)
{
    return text.find(value) != std::string_view::npos;
}

} // namespace cae
