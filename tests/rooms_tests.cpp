// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "scimate/rooms.hpp"
#include "../core/src/json.hpp"
#include <iostream>
namespace {
using namespace fixture;
void patch(Bytes& bytes, std::size_t offset, unsigned value) {
    bytes.at(offset) = value; bytes.at(offset + 1) = value >> 8;
}
void resource(Game& game, ResourceType type, unsigned number, Bytes bytes) {
    game.resources.push_back({{{type, static_cast<std::uint16_t>(number)}, 0, 0}, 0, std::move(bytes)});
}
Bytes names() {
    const std::vector<std::string> names{"species", "superClass", "info", "name", "number", "picture", "north", "east", "south", "west", "newRoom", "init"};
    Bytes out; put(out, names.size()-1, 2); out.resize(2 + names.size()*2);
    for (std::size_t i = 0; i < names.size(); ++i) {
        patch(out, 2+i*2, out.size()); put(out, names[i].size(), 2); out.insert(out.end(), names[i].begin(), names[i].end());
    }
    return out;
}
Bytes object(bool cls, const std::string& name, unsigned north, const Bytes& code = {}) {
    Bytes out; put(out, 5, 2); put(out, name.size()+5, 2);
    const auto string = out.size(); out.insert(out.end(), name.begin(), name.end()); out.push_back(0);
    const auto section = out.size(); put(out, cls ? 6 : 1, 2); put(out, 0, 2);
    put(out, 0x1234, 2); put(out, 0, 2); put(out, 0, 2); put(out, 10, 2);
    const auto pointer = out.size();
    for (auto value : {1u, cls ? 65535u : 1u, cls ? 32768u : 0u, unsigned(string), 0u, 7u, north, 0u, 0u, 0u}) put(out, value, 2);
    if (cls) for (unsigned i = 0; i < 10; ++i) put(out, i, 2);
    put(out, code.empty() ? 0 : 1, 2); if (!code.empty()) put(out, 11, 2);
    put(out, 0, 2); const auto address = out.size(); if (!code.empty()) put(out, 0, 2);
    patch(out, section+2, out.size()-section);
    if (!code.empty()) {
        put(out, 2, 2); put(out, code.size()+4, 2); patch(out, address, out.size()); out.insert(out.end(), code.begin(), code.end());
    }
    if (!cls) { put(out, 7, 2); put(out, 8, 2); put(out, 1, 2); put(out, pointer, 2); }
    put(out, 0, 2); return out;
}
void tests() {
    Game game;
    resource(game, ResourceType::vocab, 997, names());
    resource(game, ResourceType::script, 10, object(true, "Rm", 0));
    resource(game, ResourceType::script, 100, object(false, "Beach", 101, {0x39,10,0x78,0x39,101,0x54,6,0x48}));
    resource(game, ResourceType::script, 101, object(false, "Forest", 999));
    // Runtime lookup must retain the first resource, even with a malformed duplicate.
    resource(game, ResourceType::script, 100, {2,0,3,0,0,0});
    auto analysis = explore_sci0_rooms(game);
    require(analysis.rooms.size() == 2 && analysis.shadowed_entries == 1, "room discovery/duplicate policy");
    const auto& beach = analysis.rooms[0];
    require(beach.number == 100 && beach.initial_number == 0 && beach.number_basis == "export-0-convention", "number convention lost initial property");
    require(beach.picture == 7 && beach.exits.size() == 2 && beach.exits[0].target == 101, "picture/directional/send clues");
    require(analysis.rooms[1].notes.back().find("no resolved") != std::string::npos, "missing target diagnostic");
    const auto json = json_detail::parse_json(format_rooms_json(analysis, 100));
    require(json.at("rooms").array().size() == 1 && json.at("static_analysis").value == json_detail::Json(true).value, "JSON filtering/static marker");
    require(format_rooms_json(analysis) == format_rooms_json(explore_sci0_rooms(game)), "deterministic output");
    // Branches invalidate constants rather than fabricating a transition.
    game.resources[2].payload = object(false, "Beach", 101, {0x39,10,0x78,0x39,101,0x33,0,0x54,6,0x48});
    require(explore_sci0_rooms(game).rooms[0].exits.size() == 1, "branch retained speculative send");
    game.resources[0].payload.clear();
    analysis = explore_sci0_rooms(game);
    require(!analysis.diagnostics.empty() && !analysis.rooms[0].picture, "missing selectors guessed property meanings");
}
}
int main() {
    try { tests(); std::cout << "Room explorer tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
