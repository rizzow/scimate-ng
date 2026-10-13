// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
namespace scimate {
using ResourceKey = std::pair<ResourceType, std::uint16_t>;
using ResourceIndex = std::map<ResourceKey, const Resource*>;
enum class DuplicatePolicy { first, last };
// SCI0 runtime lookup uses the first map entry. Last-entry inspection is explicit.
inline ResourceIndex index_resources(const Game& game, DuplicatePolicy policy = DuplicatePolicy::first) {
    ResourceIndex result;
    for (const auto& resource : game.resources) {
        const ResourceKey key{resource.entry.id.type, resource.entry.id.number};
        if (policy == DuplicatePolicy::last) result[key] = &resource;
        else result.emplace(key, &resource);
    }
    return result;
}
}
