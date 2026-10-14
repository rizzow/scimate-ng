// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/rooms.hpp"
#include "scimate/resource_lookup.hpp"
#include "script_internal.hpp"
#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
namespace scimate {
namespace {
using namespace script_detail;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct RoomClass { unsigned parent; std::string name; std::vector<unsigned> selectors; };
std::string object_name(const Object& object, const ParsedScript& script) {
    if (object.values.size() < 4) return "object_" + hex(object.pointer);
    auto found = script.strings.find(object.values[3]);
    return found == script.strings.end() ? "object_" + hex(object.pointer) : found->second;
}
std::string normalized(std::string name) {
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
        name = name.substr(1, name.size() - 2);
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
bool room_class(const Object& object, const std::map<unsigned, RoomClass>& classes) {
    std::set<unsigned> visited;
    auto id = object.values[0];
    while (visited.insert(id).second) {
        auto found = classes.find(id);
        if (found == classes.end()) break;
        const auto name = normalized(found->second.name);
        if (name == "rm" || name == "room") return true;
        id = found->second.parent;
    }
    return false;
}
std::map<std::string, unsigned> object_properties(const Object& object,
        const std::map<unsigned, RoomClass>& classes,
        const std::map<unsigned, std::string>& names) {
    auto type = classes.find(object.values[0]);
    if (type == classes.end()) return {};
    std::map<std::string, unsigned> result;
    for (std::size_t i = 0; i < std::min(object.values.size(), type->second.selectors.size()); ++i) {
        auto name = names.find(type->second.selectors[i]);
        if (name != names.end()) result[name->second] = object.values[i];
    }
    return result;
}
// Retain constants only inside straight-line blocks. Unknown operations and all
// calls invalidate the stack; a validated send packet still isn't proven executed.
std::vector<RoomExit> constant_exits(const ParsedScript& script,
                                   const std::map<unsigned, std::string>& names) {
    std::vector<std::optional<unsigned>> stack;
    std::optional<unsigned> acc;
    std::vector<RoomExit> exits;
    std::size_t previous = 0;
    auto reset = [&] { stack.clear(); acc.reset(); };
    for (const auto& inst : script.instructions) {
        if (inst.start != previous || script.procedures.count(inst.start) || script.labels.count(inst.start)) reset();
        previous = inst.end;
        const auto op = inst.opcode;
        if (op == 26) acc = static_cast<std::uint16_t>(signed_value(inst.operands[0], inst.width));
        else if (op == 28) stack.push_back(static_cast<std::uint16_t>(signed_value(inst.operands[0], inst.width)));
        else if (op == 27) stack.push_back(acc);
        else if (op >= 59 && op <= 61) stack.push_back(op - 59);
        else if (op == 29 && !stack.empty()) stack.pop_back();
        else if (op == 30 && !stack.empty()) stack.push_back(stack.back());
        else if (op == 37 || op == 42 || op == 43) {
            const auto bytes = inst.operands.back();
            if (!(bytes % 2) && bytes / 2 <= stack.size()) {
                auto cursor = stack.size() - bytes / 2;
                const auto begin = cursor;
                bool valid = true;
                while (cursor < stack.size()) {
                    if (stack.size() - cursor < 2 || !stack[cursor] || !stack[cursor+1] ||
                        *stack[cursor+1] > stack.size() - cursor - 2) { valid = false; break; }
                    cursor += 2 + *stack[cursor+1];
                }
                for (cursor = begin; valid && cursor < stack.size();) {
                    auto name = names.find(*stack[cursor]);
                    const auto count = *stack[cursor+1];
                    if (name != names.end() && (name->second == "newRoom" || name->second == "startRoom") &&
                        count == 1 && stack[cursor+2] && *stack[cursor+2] <= 2047)
                        exits.push_back({name->second, *stack[cursor+2], inst.start,
                            "constant send at $" + hex(inst.start) + "; receiver, conditions and room ownership unresolved"});
                    cursor += 2 + count;
                }
            }
            reset();
        } else if (op == 49 || op == 46 || op == 57 || op == 40 || (op >= 64 && op <= 67)) acc.reset();
        else reset();
        if (stack.size() > 128) reset();
    }
    return exits;
}
using json_detail::Json;
Json optional_number(std::optional<unsigned> value) { return value ? Json(*value) : Json(nullptr); }
}
RoomExplorer explore_sci0_rooms(const Game& game) {
    RoomExplorer result;
    const auto resources = index_resources(game);
    result.shadowed_entries = game.resources.size() - resources.size();
    std::map<unsigned, std::string> names;
    auto vocab = resources.find({ResourceType::vocab, 997});
    if (vocab != resources.end()) {
        try {
            const auto& bytes = vocab->second->payload;
            const unsigned count = unsigned(word(bytes, 0, bytes.size())) + 1;
            require(count <= (bytes.size() - 2) / 2, "truncated selector table");
            for (unsigned i = 0; i < count; ++i) {
                auto offset = word(bytes, 2 + i * 2, bytes.size());
                if (!offset) continue;
                require(offset >= 2 + count * 2, "selector offset overlaps table");
                auto size = word(bytes, offset, bytes.size());
                require(offset + 2 + size <= bytes.size(), "truncated selector name");
                names[i] = std::string(bytes.begin() + offset + 2, bytes.begin() + offset + 2 + size);
            }
        } catch (const std::runtime_error& error) { names.clear(); result.diagnostics.push_back("vocab 997: " + std::string(error.what())); }
    }
    if (names.empty()) result.diagnostics.push_back("Selector names unavailable; room properties and exits cannot be resolved.");
    std::map<unsigned, ParsedScript> scripts;
    std::map<unsigned, RoomClass> classes;
    std::set<unsigned> ambiguous;
    for (const auto& entry : resources) if (entry.first.first == ResourceType::script) {
        try { scripts.emplace(entry.first.second, parse(entry.second->payload)); }
        catch (const std::runtime_error& error) { result.diagnostics.push_back("script " + std::to_string(entry.first.second) + ": " + error.what()); }
    }
    for (const auto& entry : scripts) for (const auto& object : entry.second.objects) if (object.is_class) {
        auto id = object.values[0];
        if (!classes.emplace(id, RoomClass{object.values[1], object_name(object, entry.second), object.selectors}).second) ambiguous.insert(id);
    }
    for (auto id : ambiguous) {
        classes.erase(id);
        result.diagnostics.push_back("Ambiguous class " + std::to_string(id) + "; room inheritance unresolved.");
    }
    for (const auto& entry : scripts) {
        std::vector<RoomObject> objects;
        for (const auto& object : entry.second.objects) if (!object.is_class) {
            auto props = object_properties(object, classes, names);
            if (props.count("view")) objects.push_back({object_name(object, entry.second), object.pointer, std::move(props)});
        }
        const auto sends = constant_exits(entry.second, names);
        for (const auto& object : entry.second.objects) if (!object.is_class && room_class(object, classes)) {
            Room room;
            room.script = static_cast<std::uint16_t>(entry.first);
            room.offset = object.pointer;
            room.id = "script_" + std::to_string(entry.first) + "_object_" + hex(object.pointer);
            room.name = object_name(object, entry.second);
            auto props = object_properties(object, classes, names);
            if (props.count("number")) room.initial_number = props.at("number");
            if (!entry.second.exports.empty() &&
                entry.second.exports.front().second == object.pointer) {
                room.number = entry.first;
                room.number_basis = "export-0-convention";
                room.notes.push_back("Room number candidate uses the exported room object's script ID (ScriptID/export-0 convention); runtime mapping is unverified.");
            } else if (room.initial_number && *room.initial_number != 0) {
                room.number = room.initial_number;
                room.number_basis = "initial-property";
            } else room.number_basis = "unresolved";
            for (const auto* name : {"picture", "pic", "picNumber"}) if (props.count(name)) { room.picture = props.at(name); break; }
            for (const auto* direction : {"north", "east", "south", "west"})
                if (props.count(direction) && props.at(direction) != 0)
                    room.exits.push_back({direction, props.at(direction), object.pointer,
                        "initial " + std::string(direction) + " property; runtime may override it"});
            room.exits.insert(room.exits.end(), sends.begin(), sends.end());
            room.objects = objects;
            room.notes.push_back("Room candidate identified by Rm/Room ancestry; initial values do not prove runtime behavior.");
            room.notes.push_back("View objects and constant sends are associated by script, not proven room ownership.");
            if (!room.number) room.notes.push_back("Room number unresolved; default zero is not treated as a runtime identity.");
            if (!room.picture) room.notes.push_back("Initial picture unresolved; no same-number picture is assumed.");
            else if (*room.picture > 2047 || !resources.count({ResourceType::pic, static_cast<std::uint16_t>(*room.picture)}))
                room.notes.push_back("Referenced initial picture is absent.");
            result.rooms.push_back(std::move(room));
        }
    }
    for (auto& room : result.rooms) for (const auto& exit : room.exits) {
        auto count = std::count_if(result.rooms.begin(), result.rooms.end(), [&](const auto& target) { return target.number == exit.target; });
        if (count != 1) room.notes.push_back("Exit " + exit.kind + " to " + std::to_string(exit.target) +
            (count == 0 ? " has no resolved room candidate." : " matches multiple room candidates."));
    }
    return result;
}
std::string format_rooms_text(const RoomExplorer& explorer, std::optional<unsigned> number) {
    std::ostringstream out;
    out << "SCI0 room explorer — static candidates, not runtime state\n";
    out << "Rooms: " << explorer.rooms.size() << "; shadowed resource entries: " << explorer.shadowed_entries << '\n';
    for (const auto& room : explorer.rooms) if (!number || room.number == number) {
        out << "\nRoom " << (room.number ? std::to_string(*room.number) : "unresolved") << " " << room.name
            << " [" << room.id << "]\n  Script " << room.script << " @ $" << hex(room.offset)
            << "; initial picture " << (room.picture ? std::to_string(*room.picture) : "unresolved") << '\n';
        out << "  Number basis: " << room.number_basis << "; initial number property "
            << (room.initial_number ? std::to_string(*room.initial_number) : "unresolved") << '\n';
        for (const auto& exit : room.exits) out << "  " << exit.kind << " -> room " << exit.target << " [likely] " << exit.evidence << '\n';
        for (const auto& object : room.objects) {
            out << "  Same-script object " << object.name << " @ $" << hex(object.offset);
            for (const auto& prop : object.properties) out << " " << prop.first << '=' << prop.second;
            out << '\n';
        }
        for (const auto& note : room.notes) out << "  Note: " << note << '\n';
    }
    for (const auto& diagnostic : explorer.diagnostics) out << "Diagnostic: " << diagnostic << '\n';
    return out.str();
}
std::string format_rooms_json(const RoomExplorer& explorer, std::optional<unsigned> number) {
    using O = Json::Object; using A = Json::Array;
    A rooms, diagnostics;
    for (const auto& room : explorer.rooms) if (!number || room.number == number) {
        A exits, objects, notes;
        for (const auto& exit : room.exits) exits.emplace_back(O{{"kind", exit.kind}, {"target", exit.target},
            {"offset", exit.offset}, {"confidence", "likely"}, {"evidence", exit.evidence}});
        for (const auto& object : room.objects) {
            O props;
            for (const auto& prop : object.properties) props[prop.first] = prop.second;
            objects.emplace_back(O{{"name", object.name}, {"offset", object.offset}, {"properties", props}});
        }
        for (const auto& note : room.notes) notes.emplace_back(note);
        rooms.emplace_back(O{{"id", room.id}, {"name", room.name}, {"script", room.script}, {"offset", room.offset},
            {"number", optional_number(room.number)}, {"picture", optional_number(room.picture)},
            {"initial_number", optional_number(room.initial_number)}, {"number_basis", room.number_basis},
            {"exits", exits}, {"objects", objects}, {"notes", notes}});
    }
    for (const auto& diagnostic : explorer.diagnostics) diagnostics.emplace_back(diagnostic);
    return json_detail::dump_json(O{{"format", "scimate-rooms"}, {"version", 1}, {"static_analysis", true},
        {"shadowed_entries", explorer.shadowed_entries}, {"rooms", rooms}, {"diagnostics", diagnostics}});
}
} // namespace scimate
