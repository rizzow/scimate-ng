// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>
namespace scimate::cli {
enum class Command { unknown, list, inspect, explain, rooms, disassemble, export_game, import_game,
                     view_export, view_import, picture_export, picture_import,
                     resource_export, resource_import, asset_export, asset_import, migrate };
struct CommandSpec {
    std::string_view name;
    Command command;
    unsigned min_arguments, max_arguments;
    bool later_formats = false, force = false;
};
inline constexpr CommandSpec command_specs[]{
    {"list", Command::list, 1, 1, true}, {"inspect", Command::inspect, 1, 1, true},
    {"explain", Command::explain, 1, 3}, {"rooms", Command::rooms, 1, 2},
    {"disassemble", Command::disassemble, 1, 1},
    {"export", Command::export_game, 2, 2, false, true}, {"import", Command::import_game, 2, 2},
    {"view-export", Command::view_export, 2, 2}, {"view-import", Command::view_import, 2, 2},
    {"picture-export", Command::picture_export, 2, 2}, {"picture-import", Command::picture_import, 2, 2},
    {"resource-export", Command::resource_export, 4, 4}, {"resource-import", Command::resource_import, 2, 2},
    {"sound-export", Command::asset_export, 2, 2}, {"sound-import", Command::asset_import, 2, 2},
    {"font-export", Command::asset_export, 2, 2}, {"font-import", Command::asset_import, 2, 2},
    {"cursor-export", Command::asset_export, 2, 2}, {"cursor-import", Command::asset_import, 2, 2},
    {"text-export", Command::asset_export, 2, 2}, {"text-import", Command::asset_import, 2, 2},
    {"migrate", Command::migrate, 2, 2, false, true}
};
inline const CommandSpec* command_spec(std::string_view name) {
    for (const auto& spec : command_specs) if (spec.name == name) return &spec;
    return nullptr;
}
inline bool resource_format(std::string_view format) {
    return format == "sci0" || format == "sci1" || format == "sci11" || format == "sci1-ega" || format == "sci1-vga";
}
}
