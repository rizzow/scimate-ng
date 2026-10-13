// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/diagnostics.hpp"
#include "scimate/resource_game.hpp"
#include <iomanip>
#include <sstream>
namespace scimate::workspace_detail {
inline constexpr const char* original_file = "original.bin";
inline constexpr const char* project_file = "project.scimate";
inline constexpr const char* map_file = "resource.map";
inline std::string record_stem(std::size_t record) {
    std::ostringstream out;
    out << std::setfill('0') << std::setw(7) << record;
    return out.str();
}
// Original bundle data is neutral; matching edits are a single authoritative edit.
inline Bytes reconcile(const Bytes& original, const Bytes& binary, const Bytes& bundle,
                       const std::string& context) {
    if (bundle == original) return binary;
    require(binary == original || binary == bundle, "conflicting binary and bundle edits: " + context);
    return bundle;
}
}
