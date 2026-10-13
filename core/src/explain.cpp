// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/explain.hpp"
#include "scimate/view.hpp"
#include "script_internal.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace scimate {
namespace {
using namespace script_detail;
using Key = std::pair<ResourceType, std::uint16_t>;
struct Script { ParsedScript parsed; };
struct Class { unsigned parent; std::string name; std::vector<unsigned> selectors; };
struct Reference { unsigned script, view; std::string evidence; bool player; };
struct Transition { unsigned script, room; std::string evidence; bool startup; };
struct Value {
    enum Kind { unknown, integer, object, player_global, class_id } kind = unknown;
    unsigned number = 0;
};
std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::string name_of(const Object& object, const ParsedScript& script) {
    if (object.values.size() > 3) {
        auto found = script.strings.find(object.values[3]);
        if (found != script.strings.end()) return found->second; // Already quoted/escaped.
    }
    return "object_$" + hex(object.pointer);
}
std::string bare_name(const std::string& name) {
    return name.size() >= 2 && name.front() == '"' ? name.substr(1, name.size()-2) : name;
}
std::map<unsigned, std::string> selectors(const Bytes& bytes) {
    std::map<unsigned, std::string> result;
    const auto count = static_cast<std::size_t>(word(bytes, 0, bytes.size())) + 1;
    if (count > (bytes.size()-2)/2) throw std::runtime_error("invalid selector vocabulary table");
    for (std::size_t i = 0; i < count; ++i) {
        auto offset = word(bytes, 2+i*2, bytes.size());
        if (!offset) continue;
        auto length = word(bytes, offset, bytes.size());
        auto begin = static_cast<std::size_t>(offset)+2;
        if (length > bytes.size()-begin) throw std::runtime_error("truncated selector name");
        // Reuse the parser's escaping to keep terminal output safe.
        result[static_cast<unsigned>(i)] = bare_name(quoted(bytes, begin, begin+length));
    }
    return result;
}
std::vector<std::string> lineage(const Object& object, const std::map<unsigned, Class>& classes) {
    std::vector<std::string> result;
    std::set<unsigned> visited;
    auto id = object.is_class ? object.values[1] : object.values[0];
    while (visited.insert(id).second) {
        auto found = classes.find(id);
        if (found == classes.end()) break;
        result.push_back(bare_name(found->second.name));
        id = found->second.parent;
    }
    return result;
}
bool has_class(const std::vector<std::string>& chain, const std::set<std::string>& names) {
    return std::any_of(chain.begin(), chain.end(), [&](const auto& name) { return names.count(lower(name)) != 0; });
}
bool player_object(const Object& object, const ParsedScript& script, const std::map<unsigned, Class>& classes) {
    auto name = lower(bare_name(name_of(object, script)));
    return name == "ego" || name == "gego" || has_class(lineage(object, classes), {"ego"});
}
std::map<std::string, unsigned> properties(const Object& object, const std::map<unsigned, Class>& classes,
                                         const std::map<unsigned, std::string>& names) {
    auto ids = object.selectors;
    if (!object.is_class) {
        auto found = classes.find(object.values[0]);
        if (found != classes.end()) ids = found->second.selectors;
    }
    std::map<std::string, unsigned> result;
    for (std::size_t i = 0; i < std::min(ids.size(), object.values.size()); ++i) {
        auto found = names.find(ids[i]);
        if (found != names.end()) result[found->second] = object.values[i];
    }
    return result;
}
std::string location(unsigned script, std::size_t offset) {
    return "script " + std::to_string(script) + " @ $" + hex(offset);
}
// A deliberately small abstract interpreter: constants are retained only within
// straight-line basic blocks. Calls, branches, unknown operations, function
// entries and branch targets invalidate state; no speculative control-flow merge.
void inspect_sends(unsigned number, const ParsedScript& script, const std::map<unsigned, Class>& classes,
                   const std::map<unsigned, std::string>& names, std::vector<Reference>& refs,
                   std::vector<Transition>& transitions, std::vector<std::pair<unsigned, std::string>>& cycles,
                   std::vector<std::pair<unsigned, std::string>>& player_bindings) {
    std::vector<Value> stack;
    Value acc;
    const Object* owner = nullptr;
    std::string method;
    std::map<std::size_t, std::pair<const Object*, unsigned>> methods;
    for (const auto& object : script.objects)
        for (std::size_t i = 0; i < object.addresses.size(); ++i)
            methods.emplace(object.addresses[i], std::make_pair(&object, object.methods[i]));
    auto reset = [&] { stack.clear(); acc = {}; };
    auto receiver_name = [&](Value receiver) {
        if (receiver.kind == Value::player_global) return std::string("global[0] (conventional SCI player global; unverified)");
        if (receiver.kind == Value::object)
            for (const auto& object : script.objects)
                if (object.pointer == receiver.number) return name_of(object, script);
        return std::string("unresolved receiver");
    };
    std::size_t previous_end = 0;
    for (const auto& inst : script.instructions) {
        if (previous_end != inst.start) { reset(); owner = nullptr; method.clear(); }
        previous_end = inst.end;
        if (script.procedures.count(inst.start)) {
            reset(); owner = nullptr; method.clear();
            auto entry = methods.find(inst.start);
            if (entry != methods.end()) {
                owner = entry->second.first;
                auto name = names.find(entry->second.second);
                if (name != names.end()) method = name->second;
            }
        }
        if (script.labels.count(inst.start)) reset();
        const auto op = inst.opcode;
        if (op == 26) acc = {Value::integer, static_cast<unsigned>(static_cast<std::uint16_t>(signed_value(inst.operands[0], inst.width)))};
        else if (op == 28) stack.push_back({Value::integer, static_cast<unsigned>(static_cast<std::uint16_t>(signed_value(inst.operands[0], inst.width)))});
        else if (op == 27) stack.push_back(acc);
        else if (op >= 59 && op <= 61) stack.push_back({Value::integer, op-59});
        else if (op == 29) { if (stack.empty()) reset(); else stack.pop_back(); }
        else if (op == 30) { if (stack.empty()) reset(); else stack.push_back(stack.back()); }
        else if (op == 57) acc = {Value::object, target(inst)};
        else if (op == 58) stack.push_back({Value::object, target(inst)});
        else if (op == 40) acc = {Value::class_id, inst.operands[0]};
        else if (op == 49) acc = {}; // Dynamic property read; preserve the send packet.
        else if (op == 46) acc = owner ? Value{Value::object, static_cast<unsigned>(owner->pointer)} : Value{};
        else if (op == 62) stack.push_back(owner ? Value{Value::object, static_cast<unsigned>(owner->pointer)} : Value{});
        else if (op == 64 || op == 68) {
            Value value = inst.operands[0] == 0 ? Value{Value::player_global, 0} : Value{};
            if (op == 64) acc = value; else stack.push_back(value);
        } else if (op >= 64 && op <= 79 && !(op & 8)) {
            // Non-indexed variable loads cannot invalidate earlier pushed constants.
            if (op & 4) stack.push_back({}); else acc = {};
        } else if (op == 80) {
            // Main script global[0] assignments are evidence, not a format guarantee.
            if (number == 0 && inst.operands[0] == 0 && acc.kind == Value::object)
                player_bindings.emplace_back(acc.number, location(number, inst.start) + " assigns " + receiver_name(acc) + " to global[0]");
        } else if (op == 37 || op == 42 || op == 43) {
            const auto bytes = inst.operands.back();
            Value receiver = op == 37 ? acc : (owner ? Value{Value::object, static_cast<unsigned>(owner->pointer)} : Value{});
            if (bytes % 2 == 0 && bytes/2 <= stack.size()) {
                std::size_t cursor = stack.size()-bytes/2;
                const auto end = stack.size();
                // Validate the entire packet before interpreting any selector.
                auto check = cursor;
                bool valid = true;
                while (check < end) {
                    if (end-check < 2 || stack[check].kind != Value::integer || stack[check+1].kind != Value::integer || stack[check+1].number > end-check-2) { valid = false; break; }
                    check += 2+stack[check+1].number;
                }
                while (valid && cursor < end) {
                    const auto selector = stack[cursor].number, count = stack[cursor+1].number;
                    auto found = names.find(selector);
                    const auto name = found == names.end() ? std::string{} : found->second;
                    auto evidence = location(number, inst.start) + " sends " + name + " to " + receiver_name(receiver);
                    bool player = receiver.kind == Value::player_global;
                    if (receiver.kind == Value::object)
                        for (const auto& object : script.objects)
                            if (object.pointer == receiver.number) player = player_object(object, script, classes);
                    if (count == 1 && stack[cursor+2].kind == Value::integer) {
                        auto value = stack[cursor+2].number;
                        if (name == "view") refs.push_back({number, value, evidence + " with constant " + std::to_string(value), player});
                        if (name == "newRoom" || name == "startRoom") {
                            bool startup = number == 0 && owner && !script.exports.empty() &&
                                script.exports.front().second == owner->pointer && (method == "init" || method == "play") &&
                                has_class(lineage(*owner, classes), {"game"});
                            transitions.push_back({number, value, evidence + " with constant " + std::to_string(value) + (owner ? " in " + name_of(*owner, script) + "." + method : ""), startup});
                        }
                    }
                    if (name == "setCycle" && count > 0) {
                        auto controller = stack[cursor+2];
                        if (controller.kind == Value::class_id) {
                            auto type = classes.find(controller.number);
                            evidence += type == classes.end() ? " with class " + std::to_string(controller.number) : " with controller " + type->second.name;
                        }
                        cycles.emplace_back(number, evidence + " (animation-controller clue; target view may change)");
                    }
                    cursor += 2+count;
                }
            }
            reset();
        } else if (op == 31) reset();
        else { reset(); if (op == 36) { owner = nullptr; method.clear(); } }
        if (stack.size() > 128) reset();
    }
}
} // namespace

std::string explain_sci0_game(const Game& game, std::optional<ResourceId> focus) {
    if (focus && focus->type != ResourceType::script && focus->type != ResourceType::view)
        throw std::runtime_error("explain supports script and view focus only");
    std::map<Key, const Resource*> resources;
    std::size_t duplicates = 0;
    for (const auto& resource : game.resources) {
        Key key{resource.entry.id.type, resource.entry.id.number};
        if (resources.count(key)) ++duplicates;
        resources[key] = &resource;
    }
    if (focus && !resources.count({focus->type, focus->number})) throw std::runtime_error("requested explain resource was not found");
    std::vector<std::string> diagnostics;
    std::map<unsigned, std::string> names;
    auto vocabulary = resources.find({ResourceType::vocab, 997});
    if (vocabulary != resources.end()) {
        try { names = selectors(vocabulary->second->payload); }
        catch (const std::runtime_error& error) { diagnostics.push_back("vocab 997: " + std::string(error.what())); }
    }
    std::map<unsigned, Script> scripts;
    std::map<unsigned, Class> classes;
    std::set<unsigned> ambiguous_classes;
    for (const auto& entry : resources) if (entry.first.first == ResourceType::script) {
        try { scripts.emplace(entry.first.second, Script{parse(entry.second->payload)}); }
        catch (const std::runtime_error& error) { diagnostics.push_back("script " + std::to_string(entry.first.second) + ": " + error.what()); }
    }
    for (const auto& entry : scripts) for (const auto& object : entry.second.parsed.objects) if (object.is_class) {
        auto id = object.values[0];
        if (classes.count(id)) ambiguous_classes.insert(id);
        else classes.emplace(id, Class{object.values[1], name_of(object, entry.second.parsed), object.selectors});
    }
    for (auto id : ambiguous_classes) {
        classes.erase(id);
        diagnostics.push_back("class " + std::to_string(id) + ": multiple definitions; inheritance/property mapping unresolved");
    }
    std::vector<Reference> refs;
    std::vector<Transition> transitions;
    std::vector<std::pair<unsigned, std::string>> cycles;
    std::map<unsigned, std::vector<std::pair<unsigned, std::string>>> bindings;
    for (const auto& entry : scripts)
        inspect_sends(entry.first, entry.second.parsed, classes, names, refs, transitions, cycles, bindings[entry.first]);
    std::ostringstream out;
    out << "SCI0 explanation (static heuristics; game execution is not simulated)\n"
        << "Labels: [fact] decoded data; [likely] convention/pattern; [unresolved] insufficient evidence.\n"
        << "Duplicate resource IDs: " << duplicates << " shadowed entries; last map entry wins.\n";
    if (names.empty()) out << "[unresolved] Selector names unavailable: property and method semantics cannot be inferred.\n";
    auto selected = [&](ResourceType type, unsigned number) { return !focus || (focus->type == type && focus->number == number); };
    out << "\nStartup\n";
    std::size_t starts = 0;
    for (const auto& transition : transitions) if (transition.startup) {
        ++starts;
        out << "  [likely] Opening-room candidate " << transition.room << ": " << transition.evidence << '\n';
    }
    if (!starts) out << "  [unresolved] No constant room transition found in a recognized script 0 export-0 Game init/play method.\n";
    out << "  [unresolved] First displayed screen and first playable room require startup-path analysis; candidates may be conditional or introductory scenes.\n";
    out << "\nScripts and objects\n";
    for (const auto& entry : scripts) if (selected(ResourceType::script, entry.first)) {
        const auto& script = entry.second.parsed;
        out << "  [fact] Script " << entry.first << ": " << script.objects.size() << " objects, " << script.exports.size() << " exports\n";
        for (const auto& object : script.objects) {
            auto chain = lineage(object, classes);
            auto props = properties(object, classes, names);
            const auto identity = location(entry.first, object.pointer) + " " + name_of(object, script);
            out << "    [fact] " << (object.is_class ? "Class " : "Instance ") << identity << '\n';
            if (!chain.empty()) {
                out << "      [fact] Inherits";
                for (const auto& name : chain) out << ' ' << name;
                out << '\n';
            }
            bool player = player_object(object, script, classes);
            for (const auto& binding : bindings[entry.first]) if (binding.first == object.pointer) {
                player = true;
                out << "      [likely] Player object: " << binding.second << " (conventional player global)\n";
            }
            if (player_object(object, script, classes)) out << "      [likely] Player object: Ego ancestry or exact ego/gEgo object name\n";
            const bool room = has_class(chain, {"room", "rm"}) ||
                (object.is_class && (lower(bare_name(name_of(object, script))) == "room" || lower(bare_name(name_of(object, script))) == "rm"));
            if (!object.is_class && room) out << "      [likely] Room instance: Room/Rm ancestry; script ID is not proof of room number\n";
            if (room && !script.exports.empty() && script.exports.front().second == object.pointer)
                out << "      [likely] Room script: export 0 identifies this room object\n";
            for (const auto& prop : props) if (prop.first == "view" || prop.first == "pic" || prop.first == "picture" || prop.first == "picNumber" || prop.first == "loop" || prop.first == "cel" || prop.first == "north" || prop.first == "south" || prop.first == "east" || prop.first == "west") {
                out << "      [fact] Initial " << prop.first << " = " << prop.second << '\n';
                if (prop.first == "view") refs.push_back({entry.first, prop.second, identity + " initial view property", player && !object.is_class});
                if (prop.first == "pic" || prop.first == "picture" || prop.first == "picNumber")
                    out << "      [likely] Background picture reference " << prop.second << (resources.count({ResourceType::pic, static_cast<std::uint16_t>(prop.second)}) ? " (resource exists)" : " (resource absent)") << '\n';
            }
        }
    }
    // For view-only focus, still gather property references from every script.
    if (focus && focus->type == ResourceType::view)
        for (const auto& entry : scripts) for (const auto& object : entry.second.parsed.objects) {
            auto props = properties(object, classes, names);
            auto view = props.find("view");
            if (view == props.end()) continue;
            bool player = player_object(object, entry.second.parsed, classes);
            for (const auto& binding : bindings[entry.first]) if (binding.first == object.pointer) player = true;
            refs.push_back({entry.first, view->second, location(entry.first, object.pointer) + " " + name_of(object, entry.second.parsed) + " initial view property", player && !object.is_class});
        }
    auto related_script = [&](unsigned number) {
        if (!focus) return true;
        if (focus->type == ResourceType::script) return number == focus->number;
        return std::any_of(refs.begin(), refs.end(), [&](const auto& ref) { return ref.script == number && ref.view == focus->number; });
    };
    out << "\nRoom transitions and animation controllers\n";
    for (const auto& transition : transitions) if (related_script(transition.script))
        out << "  [fact] Constant room call " << transition.room << ": " << transition.evidence << " (execution not proven)\n";
    for (const auto& cycle : cycles) if (related_script(cycle.first))
        out << "  [likely] " << cycle.second << '\n';
    if (transitions.empty() && cycles.empty()) out << "  [unresolved] No recognized constant room calls or setCycle packets.\n";
    out << "\nViews\n";
    for (const auto& entry : resources) if (entry.first.first == ResourceType::view && selected(ResourceType::view, entry.first.second)) {
        const auto number = entry.first.second;
        try {
            const auto view = read_sci0_view(entry.second->payload);
            bool multiple = false;
            out << "  [fact] View " << number << ": " << view.loops.size() << " loops\n";
            for (std::size_t i = 0; i < view.loops.size(); ++i) {
                const auto& loop = view.loops[i];
                const auto& cels = loop.mirror_of < 0 ? loop.cels : view.loops[static_cast<std::size_t>(loop.mirror_of)].cels;
                multiple = multiple || cels.size() > 1;
                out << "    [fact] Loop " << i << ": " << cels.size() << " cels";
                if (loop.mirror_of >= 0) out << ", mirrors loop " << loop.mirror_of;
                out << '\n';
            }
            out << (multiple ? "    [likely] Animation-capable: multiple cels in a loop; may also be poses/variants.\n" : "    [unresolved] Single-cel loops do not prove a static prop; scripts can switch loops/views.\n");
            if (view.loops.size() == 4 && multiple) out << "    [likely] Directional walking layout: four loops with multiple cels (weak shape heuristic).\n";
            bool referenced = false;
            for (const auto& ref : refs) if (ref.view == number) {
                referenced = true;
                out << "    " << (ref.player ? "[likely] Player view: " : "[fact] Referenced: ") << ref.evidence << '\n';
            }
            if (!referenced) out << "    [unresolved] No recognized reference; dynamic or unsupported uses may exist.\n";
        } catch (const std::runtime_error& error) { diagnostics.push_back("view " + std::to_string(number) + ": " + error.what()); }
    }
    out << "\nLimits\n  Dynamic resource IDs, interprocedural flow, animation timing and gameplay reachability are unresolved.\n"
        << "  Only decoded map/volume resources are considered; external patches are not loaded.\n";
    if (!diagnostics.empty()) {
        out << "\nDiagnostics (partial analysis)\n";
        for (const auto& diagnostic : diagnostics) out << "  " << diagnostic << '\n';
    }
    return out.str();
}
} // namespace scimate
