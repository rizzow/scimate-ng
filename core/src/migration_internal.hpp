// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/sci1.hpp"
#include "script_internal.hpp"
namespace scimate::migration_detail {
using Resources = std::map<std::pair<ResourceType, std::uint16_t>, Resource>;
using ScriptMetadata = std::map<std::uint16_t, script_detail::ParsedScript>;
struct MigrationStats {
    std::size_t scripts = 0, remapped = 0, bridged = 0, sound_calls = 0;
    std::size_t parser_calls = 0, opaque_exports = 0, unusual_file_frames = 0;
    std::vector<std::string> legacy_branches;
};
script_detail::ParsedScript parse_migration_script(const Resource& resource);
std::string migration_report(std::size_t resource_count, const MigrationStats& stats,
                             const std::vector<std::string>& selected_fallbacks, bool vga = false);
std::vector<std::string> auxiliary_references(const Game& game);
std::string object_layout_diagnostics(const ScriptMetadata& scripts);
}
