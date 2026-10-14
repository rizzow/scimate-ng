// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <optional>
#include <string>
namespace scimate {
struct RoomObject {
    std::string name;
    std::size_t offset = 0;
    std::map<std::string, unsigned> properties;
};
struct RoomExit {
    std::string kind; // north/east/south/west, or constant newRoom/startRoom send.
    unsigned target = 0; // Room number, not necessarily a script resource ID.
    std::size_t offset = 0;
    std::string evidence;
};
struct Room {
    std::string id, name; // Stable script/object identity, even without a number.
    std::uint16_t script = 0;
    std::size_t offset = 0;
    std::optional<unsigned> number, picture;
    std::optional<unsigned> initial_number;
    std::string number_basis; // initial-property, export-0-convention, unresolved.
    std::vector<RoomExit> exits;
    std::vector<RoomObject> objects; // Initial view-bearing instances in same script.
    std::vector<std::string> notes;
};
struct RoomExplorer {
    std::vector<Room> rooms;
    std::vector<std::string> diagnostics;
    std::size_t shadowed_entries = 0;
};
// SCI0 static analysis; no interpreter execution. First duplicate resource wins.
RoomExplorer explore_sci0_rooms(const Game& game);
std::string format_rooms_text(const RoomExplorer& explorer,
                              std::optional<unsigned> number = std::nullopt);
std::string format_rooms_json(const RoomExplorer& explorer,
                              std::optional<unsigned> number = std::nullopt);
} // namespace scimate
