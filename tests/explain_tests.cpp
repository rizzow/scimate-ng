// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "scimate/explain.hpp"
#include "scimate/view.hpp"
#include "../core/src/json.hpp"
#include <algorithm>
#include <iostream>

namespace {
using namespace fixture;
void has(const std::string& text, const std::string& expected) {
    if (text.find(expected) == std::string::npos) throw std::runtime_error("missing: " + expected + "\n" + text);
}
void lacks(const std::string& text, const std::string& unexpected) {
    if (text.find(unexpected) != std::string::npos) throw std::runtime_error("unexpected: " + unexpected + "\n" + text);
}
void patch(Bytes& bytes, std::size_t offset, unsigned value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset+1) = static_cast<std::uint8_t>(value >> 8);
}
void resource(Game& game, ResourceType type, unsigned number, Bytes bytes) {
    game.resources.push_back({{{type, static_cast<std::uint16_t>(number)}, 0, 0}, 0, std::move(bytes)});
}
void replace_resource(Game& game, ResourceType type, unsigned number, Bytes bytes) {
    auto found = std::find_if(game.resources.begin(), game.resources.end(), [&](const auto& entry) {
        return entry.entry.id.type == type && entry.entry.id.number == number;
    });
    require(found != game.resources.end(), "replacement fixture resource not found");
    found->payload = std::move(bytes);
}
Bytes vocabulary() {
    const std::vector<std::string> names{"species", "superClass", "info", "name", "view", "picNumber", "init", "play", "newRoom", "setCycle"};
    Bytes bytes;
    put(bytes, names.size()-1, 2);
    bytes.resize(2+2*names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        patch(bytes, 2+i*2, bytes.size());
        put(bytes, names[i].size(), 2);
        bytes.insert(bytes.end(), names[i].begin(), names[i].end());
    }
    return bytes;
}
// Build a named class/instance and optionally a method/export. All addresses
// are real script offsets, exercising property mapping and packet inspection.
Bytes object_script(bool is_class, unsigned species, unsigned parent, const std::string& name,
                    unsigned view, unsigned pic, const Bytes& code = {}, bool exported = false) {
    Bytes bytes;
    put(bytes, 5, 2); put(bytes, name.size()+5, 2);
    const auto name_offset = bytes.size();
    bytes.insert(bytes.end(), name.begin(), name.end()); bytes.push_back(0);
    const auto section = bytes.size();
    put(bytes, is_class ? 6 : 1, 2); put(bytes, 0, 2);
    put(bytes, 0x1234, 2); put(bytes, 0, 2); put(bytes, 0, 2); put(bytes, 6, 2);
    const auto pointer = bytes.size();
    for (auto value : {species, parent, is_class ? 0x8000u : 0u, static_cast<unsigned>(name_offset), view, pic}) put(bytes, value, 2);
    if (is_class) for (unsigned i = 0; i < 6; ++i) put(bytes, i, 2);
    put(bytes, code.empty() ? 0 : 1, 2);
    if (!code.empty()) put(bytes, 6, 2); // init selector
    put(bytes, 0, 2);
    const auto address_slot = bytes.size();
    if (!code.empty()) put(bytes, 0, 2);
    patch(bytes, section+2, bytes.size()-section);
    if (!code.empty()) {
        put(bytes, 2, 2); put(bytes, code.size()+4, 2);
        patch(bytes, address_slot, bytes.size());
        bytes.insert(bytes.end(), code.begin(), code.end());
    }
    if (exported) {
        put(bytes, 7, 2); put(bytes, 8, 2); put(bytes, 1, 2); put(bytes, pointer, 2);
    }
    put(bytes, 0, 2);
    return bytes;
}
Game example() {
    Game game;
    resource(game, ResourceType::vocab, 997, vocabulary());
    resource(game, ResourceType::script, 10, object_script(true, 1, 0xffff, "Game", 0, 0));
    resource(game, ResourceType::script, 11, object_script(true, 2, 0xffff, "Ego", 0, 0));
    resource(game, ResourceType::script, 12, object_script(true, 3, 0xffff, "Room", 0, 0));
    resource(game, ResourceType::script, 13, object_script(true, 4, 2, "Hero", 0, 0));
    resource(game, ResourceType::script, 100, object_script(false, 3, 3, "Beach", 42, 7, {}, true));
    resource(game, ResourceType::script, 20, object_script(false, 4, 4, "Larry", 42, 0));
    // newRoom(100) to self, followed by ret. Game instance exported at slot 0.
    resource(game, ResourceType::script, 0, object_script(false, 1, 1, "LSL", 0, 0, {0x39,8,0x78,0x39,100,0x54,6,0x48}, true));
    View view;
    view.loops.resize(4);
    for (auto& loop : view.loops) loop.cels = {{1,1,0,0,0,{1}}, {1,1,0,0,0,{2}}};
    resource(game, ResourceType::view, 42, write_sci0_view(view));
    resource(game, ResourceType::pic, 7, {});
    return game;
}
void tests() {
    auto game = example();
    auto report = explain_sci0_game(game);
    has(report, "Opening-room candidate 100");
    has(report, "Room instance: Room/Rm ancestry");
    has(report, "Room script: export 0 identifies this room object");
    has(report, "Initial picNumber = 7");
    has(report, "Background picture reference 7 (resource exists)");
    has(report, "Inherits Hero Ego");
    has(report, "Player view:");
    has(report, "Directional walking layout");
    has(report, "First displayed screen and first playable room require startup-path analysis");
    require(report == explain_sci0_game(game), "nondeterministic explain output");
    auto focused = explain_sci0_game(game, ResourceId{ResourceType::view,42});
    has(focused, "Larry"); has(focused, "Player view:"); lacks(focused, "[fact] Script 100:");
    focused = explain_sci0_game(game, ResourceId{ResourceType::script,100});
    has(focused, "Room instance"); lacks(focused, "[fact] Script 20:"); lacks(focused, "[fact] View 42:");
    fails([&] { explain_sci0_game(game, ResourceId{ResourceType::view,999}); }, "not found");
    has(explain_sci0_game(game, ResourceId{ResourceType::pic,7}), "script 100 -> pic 7");
    const auto analysis = analyze_sci0_game(game);
    auto references = [](const GameExplanation& value, unsigned script, ResourceType type, unsigned number) {
        return std::count_if(value.references.begin(), value.references.end(), [&](const auto& ref) {
            return ref.source.type == ResourceType::script && ref.source.number == script &&
                ref.target.type == type && ref.target.number == number;
        });
    };
    require(references(analysis, 100, ResourceType::pic, 7) == 1, "picture reverse lookup");
    require(references(analysis, 20, ResourceType::view, 42) == 1, "view reverse lookup");
    require(references(analysis, 0, ResourceType::script, 100) == 0, "room number became a script reference");
    require(references(analysis, 20, ResourceType::script, 13) == 1, "class dependency missing");
    const auto json = format_explanation_json(analysis);
    const auto parsed = json_detail::parse_json(json);
    require(parsed.at("schema_version").number(1, 1) == 1, "JSON schema version");
    require(parsed.at("references").array().size() == analysis.references.size(), "JSON reference loss");
    require(parsed.at("nodes").array().size() == analysis.nodes.size(), "JSON evidence loss");
    require(json == format_explanation_json(analyze_sci0_game(game)), "nondeterministic JSON");
    auto calls = example();
    resource(calls, ResourceType::script, 21, object_script(false, 4, 4, "Caller", 0, 0,
        {0x47,20,0,0,0x45,0,0,0x48}));
    const auto callAnalysis = analyze_sci0_game(calls);
    require(references(callAnalysis, 21, ResourceType::script, 20) == 1, "calle script reference");
    require(references(callAnalysis, 21, ResourceType::script, 0) == 1, "callb script reference");
    // First map entry retains runtime semantics even if a later duplicate is malformed.
    resource(game, ResourceType::script, 0, {2,0,3,0,0,0});
    report = explain_sci0_game(game);
    has(report, "Opening-room candidate 100"); has(report, "1 shadowed entries");
    require(references(analyze_sci0_game(game), 0, ResourceType::script, 10) > 0, "first script references were shadowed");
    // A selector number cannot confer semantics without its vocabulary.
    for (auto& entry : game.resources)
        if (entry.entry.id.type == ResourceType::vocab && entry.entry.id.number == 997) entry.payload.clear();
    report = explain_sci0_game(game); has(report, "Selector names unavailable"); lacks(report, "Player view:");
    for (auto& entry : game.resources)
        if (entry.entry.id.type == ResourceType::view && entry.entry.id.number == 42) entry.payload.clear();
    report = explain_sci0_game(game); has(report, "view 42: SCI0 view:");
    // Dynamic arguments, incomplete packets, branches, and calls must never
    // manufacture a constant startup target from unrelated stack literals.
    for (const auto& code : std::vector<Bytes>{
             {0x39,8,0x78,0x81,1,0x36,0x54,6,0x48}, // unknown global argument
             {0x39,8,0x7a,0x39,100,0x54,6,0x48}, // claims two args but supplies one
             {0x39,8,0x78,0x39,100,0x33,0,0x54,6,0x48}, // branch invalidates constants
             {0x39,8,0x78,0x39,100,0x43,0,0,0x54,6,0x48}}) { // call invalidates constants
        auto sample = example();
        replace_resource(sample, ResourceType::script, 0, object_script(false, 1, 1, "LSL", 0, 0, code, true));
        lacks(explain_sci0_game(sample), "Opening-room candidate");
    }
    // A Game init method not exported as the main game is not startup evidence.
    auto unexported = example();
    replace_resource(unexported, ResourceType::script, 0, object_script(false, 1, 1, "LSL", 0, 0, {0x39,8,0x78,0x39,100,0x54,6,0x48}));
    lacks(explain_sci0_game(unexported), "Opening-room candidate");
    // Both runtime view assignment and animation-controller sends are recognized.
    auto runtime = example();
    resource(runtime, ResourceType::script, 21, object_script(false, 4, 4, "Larry", 0, 0,
        {0x39,4,0x78,0x39,42,0x54,6,0x39,9,0x78,0x51,2,0x36,0x54,6,0x48}));
    report = explain_sci0_game(runtime, ResourceId{ResourceType::view,42});
    has(report, "sends view to \"Larry\" with constant 42"); has(report, "sends setCycle");
    require(references(analyze_sci0_game(runtime), 21, ResourceType::view, 42) == 1, "constant view send missing");
    for (const auto& code : std::vector<Bytes>{
             {0x39,4,0x78,0x39,42,0x33,0,0x54,6,0x48},
             {0x39,4,0x78,0x81,1,0x36,0x54,6,0x48},
             {0x39,4,0x7a,0x39,42,0x54,6,0x48}}) {
        replace_resource(runtime, ResourceType::script, 21, object_script(false, 4, 4, "Larry", 0, 0, code));
        require(references(analyze_sci0_game(runtime), 21, ResourceType::view, 42) == 0, "unsupported send manufactured a resource link");
    }
    // Names are escaped before being emitted into the terminal.
    resource(runtime, ResourceType::script, 22, object_script(false, 4, 4, "bad\nname", 42, 0));
    report = explain_sci0_game(runtime); has(report, "bad\\nname"); lacks(report, "bad\nname");
}
} // namespace
int main() {
    try { tests(); std::cout << "SCI0 explanation tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
