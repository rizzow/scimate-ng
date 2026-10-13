// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <string>
namespace scimate {
struct ResourceJsonBundle { std::string json, schema; bool semantic = false; };
ResourceJsonBundle export_resource_json_bundle(ResourceId id, const Bytes& payload);
// Versioned UTF-8 JSON. Semantic layouts where known; explicitly labelled byte
// records for opaque layouts. Immutable identities and fixed script addresses.
std::string export_resource_json(ResourceId id,const Bytes& payload);
std::string resource_json_schema(ResourceId id,const Bytes& payload);
Bytes import_resource_json(ResourceId id,const Bytes& original,const std::string& json);
bool resource_json_is_semantic(ResourceId id,const Bytes& payload);
}
