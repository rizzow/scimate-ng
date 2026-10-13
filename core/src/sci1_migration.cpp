// SPDX-License-Identifier: GPL-2.0-or-later
// Compatibility boundaries derived from ScummVM kernel/features/script sources.
#include "scimate/sci1.hpp"
#include "scimate/sound.hpp"
#include "scimate/assets.hpp"
#include "script_internal.hpp"
#include <array>
#include <sstream>

namespace scimate {
namespace {
using namespace script_detail;
void put(Bytes& bytes, unsigned value) {
    bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void patch(Bytes& bytes, std::size_t offset, unsigned value) {
    if (value > 65535 || offset > bytes.size() || bytes.size()-offset < 2) throw std::runtime_error("migration address outside 16-bit script");
    bytes[offset] = static_cast<std::uint8_t>(value); bytes[offset+1] = static_cast<std::uint8_t>(value >> 8);
}
// This is a ScummVM compatibility profile, not a native SCI1 sound port.
// Validate the source ABI and resource encoding independently of the runtime
// dispatch guard. ScummVM selects SCI0 late sound when nodePtr is absent.
void validate_sound_dispatch(const Game& game) {
    const Bytes* selectors = nullptr;
    for (const auto& resource : game.resources) {
        if (resource.entry.id.type == ResourceType::vocab && resource.entry.id.number == 997 && !selectors)
            selectors = &resource.payload;
    }
    if (!selectors) throw std::runtime_error("SCI0 sound compatibility requires selector vocabulary 997 to verify ScummVM dispatch");
    const auto& bytes = *selectors;
    const auto count = unsigned(word(bytes, 0, bytes.size())) + 1;
    if (count > (bytes.size()-2)/2) throw std::runtime_error("truncated selector vocabulary 997");
    for (unsigned i = 0; i < count; ++i) {
        const auto offset = word(bytes, 2+2*i, bytes.size());
        if (offset < 2+2*count) throw std::runtime_error("selector name overlaps vocabulary 997 offset table");
        const auto length = word(bytes, offset, bytes.size());
        if (offset+2u+length > bytes.size()) throw std::runtime_error("truncated selector name in vocabulary 997");
        if (length == 7 && std::string(bytes.begin()+offset+2, bytes.begin()+offset+2+length) == "nodePtr")
            throw std::runtime_error("SCI0 sound compatibility cannot preserve late sound dispatch with nodePtr present; early sound needs a semantic port");
    }
    std::set<std::uint16_t> seen_sounds;
    for (const auto& resource : game.resources) if (resource.entry.id.type == ResourceType::sound &&
                                                  seen_sounds.insert(resource.entry.id.number).second) {
        try {
            const auto sound = read_sci0_sound(resource.payload);
            if (sound.header[0] == 2 && sound.sample_header.empty())
                throw std::runtime_error("sample flag set without a digital sample header");
        }
        catch (const std::exception& error) {
            throw std::runtime_error("sound " + std::to_string(resource.entry.id.number) + ": " + error.what());
        }
    }
}
void validate_sound_call(const ParsedScript& parsed, std::size_t index) {
    const auto& call = parsed.instructions.at(index);
    const auto frame = call.operands.at(1);
    if (frame < 2 || frame > 6 || frame % 2)
        throw std::runtime_error("unsupported SCI0 DoSound argument frame at " + std::to_string(call.start));
    // Walk a contiguous, side-effect-free push sequence. Never infer a subop
    // from the nearest pushi: object arguments may also use immediate values.
    std::vector<int> pushed;
    auto cursor = call.start;
    const auto needed = frame/2+1; // Includes the argument-count stack word.
    while (pushed.size() < needed && index > 0) {
        const auto& inst = parsed.instructions.at(--index);
        if (inst.end != cursor || parsed.labels.count(cursor) || parsed.procedures.count(cursor)) break;
        cursor = inst.start;
        int value = -1;
        if (inst.opcode == 28) value = static_cast<int>(inst.operands.at(0));
        else if (inst.opcode >= 59 && inst.opcode <= 61) value = static_cast<int>(inst.opcode-59);
        else if (inst.opcode == 27 || inst.opcode == 51 || inst.opcode == 55 || inst.opcode == 56 ||
                 inst.opcode == 58 || inst.opcode == 62 || (inst.opcode >= 68 && inst.opcode <= 71)) {}
        else if (inst.opcode == 26 || inst.opcode == 40 || inst.opcode == 46 || inst.opcode == 49 ||
                 inst.opcode == 57 || (inst.opcode >= 64 && inst.opcode <= 67)) continue;
        else break;
        pushed.push_back(value);
    }
    if (pushed.size() != needed || pushed.back() != static_cast<int>(frame/2))
        throw std::runtime_error("SCI0 DoSound frame cannot be proven statically at " + std::to_string(call.start));
    const auto subop = pushed.at(pushed.size()-2);
    constexpr std::array<unsigned,13> min_args{2,2,1,2,1,2,2,1,1,2,2,1,1};
    constexpr std::array<unsigned,13> max_args{2,2,2,2,2,2,2,1,2,2,2,1,1};
    if (subop < 0 || subop > 12 || frame/2 < min_args.at(static_cast<unsigned>(subop)) ||
        frame/2 > max_args.at(static_cast<unsigned>(subop)))
        throw std::runtime_error("unsupported SCI0 DoSound suboperation/frame at " + std::to_string(call.start));
}
using Resources = std::map<std::pair<ResourceType,std::uint16_t>,Resource>;
ParsedScript parse_migration_script(const Resource& resource) {
    try {
        // Only script 0 changes layout. Other scripts retain every instruction
        // address, allowing legacy conditional/jump operands to pass through
        // verbatim without claiming their runtime paths are safe.
        auto parsed = parse(resource.payload,resource.entry.id.number == 0,false);
        for (const auto& inst : parsed.instructions)
            if (inst.opcode == 32 && !parsed.boundaries.count(target(inst)))
                throw std::runtime_error("local call at byte " + std::to_string(inst.start) + " does not target a decoded instruction");
        return parsed;
    }
    catch (const std::exception& error) {
        throw std::runtime_error("script " + std::to_string(resource.entry.id.number) + ": " + error.what());
    }
}
std::map<std::uint16_t,ParsedScript> validate_migration_scripts(const Resources& resources) {
    std::map<std::uint16_t,ParsedScript> scripts;
    for (const auto& entry : resources) if (entry.first.first == ResourceType::script)
        scripts.emplace(entry.first.second,parse_migration_script(entry.second));
    for (const auto& entry : scripts) for (const auto& inst : entry.second.instructions) {
        if (inst.opcode != 34 && inst.opcode != 35) continue;
        const auto script_number = inst.opcode == 34 ? 0u : inst.operands.at(0);
        const auto export_number = inst.operands.at(inst.opcode == 34 ? 0 : 1);
        const auto destination = scripts.find(static_cast<std::uint16_t>(script_number));
        const auto context = "script " + std::to_string(entry.first) + " at byte " + std::to_string(inst.start) +
                             ": direct call to script " + std::to_string(script_number) + " export " + std::to_string(export_number);
        if (destination == scripts.end() || export_number >= destination->second.exports.size())
            throw std::runtime_error(context + " has no export slot");
        if (!destination->second.boundaries.count(destination->second.exports.at(export_number).second))
            throw std::runtime_error(context + " does not target a decoded instruction");
    }
    return scripts;
}
void adapt_parser(Resources& resources) {
    if (resources.count({ResourceType::vocab,10}) || resources.count({ResourceType::vocab,910}) || resources.count({ResourceType::vocab,911}))
        throw std::runtime_error("parser migration: secondary-language vocabularies require a separate conversion");
    const auto source = resources.find({ResourceType::vocab,0});
    const auto grammar = resources.find({ResourceType::vocab,900});
    if (source == resources.end() || grammar == resources.end())
        throw std::runtime_error("parser migration requires SCI0 word vocabulary 0 and grammar vocabulary 900");
    if (grammar->second.payload.empty() || grammar->second.payload.size()%20)
        throw std::runtime_error("parser migration: grammar must contain complete 20-byte rules");
    if (resources.count({ResourceType::vocab,902}))
        throw std::runtime_error("parser migration: vocabulary 902 conflicts with the target suffix resource");
    // SCI0 keeps the final definition of a repeated spelling. SCI1 supports
    // ambiguity, so collapse duplicates explicitly to retain SCI0 semantics.
    std::map<std::string,Word> words;
    for (const auto& entry : read_sci0_vocabulary(source->second.payload)) words[entry.text] = entry;
    if (words.empty()) throw std::runtime_error("parser migration: word vocabulary is empty");
    Bytes converted(510,0); // 255 little-endian initial-character offsets.
    for (const auto& entry : words) {
        const auto& word_entry = entry.second;
        if (word_entry.text.empty() || word_entry.text.size() > 254)
            throw std::runtime_error("parser migration: unsupported word length");
        const auto initial = static_cast<unsigned char>(word_entry.text.front());
        if (initial == 0 || initial == 255 || converted.size()+word_entry.text.size()+5 > 65531)
            throw std::runtime_error("parser migration: vocabulary exceeds SCI1 limits");
        if (word(converted,2*initial,510) == 0) patch(converted,2*initial,static_cast<unsigned>(converted.size()));
        converted.push_back(0); // Full spelling, no preceding-word prefix.
        converted.insert(converted.end(),word_entry.text.begin(),word_entry.text.end());
        converted.push_back(0);
        converted.push_back(static_cast<std::uint8_t>(word_entry.word_class >> 4));
        converted.push_back(static_cast<std::uint8_t>((word_entry.word_class << 4) | (word_entry.group >> 8)));
        converted.push_back(static_cast<std::uint8_t>(word_entry.group));
    }
    const auto suffix = resources.find({ResourceType::vocab,901});
    if (suffix != resources.end()) {
        auto moved = suffix->second;
        moved.entry.id.number = 902;
        resources.erase(suffix);
        resources.emplace(std::make_pair(ResourceType::vocab,std::uint16_t(902)),std::move(moved));
    }
    auto moved_grammar = grammar->second;
    moved_grammar.entry.id.number = 901;
    resources.erase(grammar);
    resources.emplace(std::make_pair(ResourceType::vocab,std::uint16_t(901)),std::move(moved_grammar));
    auto moved_words = source->second;
    moved_words.entry.id.number = 900;
    moved_words.payload = std::move(converted);
    resources.erase(source);
    resources.emplace(std::make_pair(ResourceType::vocab,std::uint16_t(900)),std::move(moved_words));
    // SCI0's debug kernel-name table describes the old ABI and also makes
    // ScummVM detect SCI0. SCI1 permits its absence and uses the standard ABI.
    resources.erase({ResourceType::vocab,999});
    bool view_present = false;
    for (const auto& entry : resources) if (entry.first.first == ResourceType::view) {
        (void)read_sci0_view(entry.second.payload);
        if (entry.first.second < 1000) view_present = true;
    }
    if (!view_present) throw std::runtime_error("SCI1 EGA parser migration requires a valid EGA view for interpreter detection");
}
// Expand the original main export table and relocate absolute addresses. Code
// references are relative, so an equal shift of instruction and target cancels.
// Unsupported source targets are rejected by strict parsing before relocation.
Bytes add_file_bridges(const Bytes& original, const ParsedScript& parsed, unsigned first_export) {
    const Section* table = nullptr;
    for (const auto& section : parsed.sections) if (section.type == 7) {
        if (table) throw std::runtime_error("migration requires a single main export table");
        table = &section;
    }
    if (!table) throw std::runtime_error("main script has no export table");
    constexpr unsigned added = 8;
    const auto threshold = table->end;
    auto moved = [&](unsigned offset) {
        return offset >= threshold && offset < original.size() ? offset+added : offset;
    };
    Bytes bytes(original.begin(), original.begin()+static_cast<std::ptrdiff_t>(threshold));
    bytes.insert(bytes.end(), added, 0);
    const auto last = parsed.sections.back().end;
    bytes.insert(bytes.end(), original.begin()+static_cast<std::ptrdiff_t>(threshold), original.begin()+static_cast<std::ptrdiff_t>(last));
    if (bytes.size()+64 > 65535) throw std::runtime_error("main script has insufficient space for file-I/O bridges");
    patch(bytes, table->start+2, static_cast<unsigned>(table->end-table->start)+added);
    patch(bytes, table->start+4, first_export+4);
    for (const auto& entry : parsed.exports) patch(bytes, moved(static_cast<unsigned>(entry.first)), moved(entry.second));
    for (const auto& object : parsed.objects) {
        auto address_table = object.pointer + 2*object.values.size() + 2*object.selectors.size() + 4 + 2*object.methods.size();
        for (std::size_t i = 0; i < object.addresses.size(); ++i)
            patch(bytes, moved(static_cast<unsigned>(address_table+2*i)), moved(object.addresses[i]));
        // Name pointers are metadata even if the compiler omitted relocation.
        if (object.values.size() > 3) patch(bytes, moved(static_cast<unsigned>(object.pointer+6)), moved(object.values[3]));
    }
    for (const auto& section : parsed.sections) if (section.type == 8) {
        auto count = word(original, section.start+4, section.end);
        for (unsigned i = 0; i < count; ++i) {
            auto entry = section.start+6+i*2;
            auto slot = word(original, entry, section.end);
            patch(bytes, moved(static_cast<unsigned>(entry)), moved(slot));
            patch(bytes, moved(slot), moved(word(original, slot, original.size())));
        }
    }
    for (const auto& inst : parsed.instructions) if (inst.opcode == 57 || inst.opcode == 58 || inst.opcode == 32 || (inst.opcode >= 23 && inst.opcode <= 25)) {
        auto target_offset = target(inst);
        auto delta = static_cast<std::uint16_t>(moved(target_offset)-moved(static_cast<unsigned>(inst.end)));
        auto slot = moved(static_cast<unsigned>(inst.start+1));
        if (inst.width == 1) {
            auto signed_delta = static_cast<std::int16_t>(delta);
            if (signed_delta < -128 || signed_delta > 127) throw std::runtime_error("main relocation requires widening an instruction; unsupported");
            bytes.at(slot) = static_cast<std::uint8_t>(delta);
        } else patch(bytes, slot, delta);
    }
    // FileIO suboperations use exactly the same ScummVM implementations as
    // SCI0 FOpen/FPuts/FGets/FClose. Bridges preserve argument order and result.
    constexpr std::array<unsigned,4> subops{0,6,5,1}, counts{2,2,3,1};
    Bytes code;
    for (unsigned i = 0; i < 4; ++i) {
        patch(bytes, threshold+i*2, static_cast<unsigned>(bytes.size()+4+code.size()));
        code.insert(code.end(), {0x39, static_cast<std::uint8_t>(counts[i]+1), 0x39, static_cast<std::uint8_t>(subops[i])});
        for (unsigned argument = 1; argument <= counts[i]; ++argument)
            code.insert(code.end(), {0x8f, static_cast<std::uint8_t>(argument)}); // lsp
        code.insert(code.end(), {0x43,0x74,static_cast<std::uint8_t>(2*(counts[i]+1)),0x48});
    }
    put(bytes, 2); put(bytes, static_cast<unsigned>(code.size()+4));
    bytes.insert(bytes.end(), code.begin(), code.end()); put(bytes, 0);
    return bytes;
}
} // namespace
Sci1Migration migrate_sci0_to_sci1(const Game& game) {
    using Key = std::pair<ResourceType, std::uint16_t>;
    std::map<Key, Resource> unique;
    // SCI0 ScummVM lookup keeps the first map entry for duplicate IDs.
    for (const auto& resource : game.resources) unique.emplace(Key{resource.entry.id.type,resource.entry.id.number}, resource);
    auto main = unique.find({ResourceType::script,0});
    if (main == unique.end()) throw std::runtime_error("migration requires script 0");
    const auto script_metadata = validate_migration_scripts(unique);
    const auto& parsed_main = script_metadata.at(0);
    if (parsed_main.exports.empty()) throw std::runtime_error("migration requires main export 0");
    bool main_object = false;
    for (const auto& object : parsed_main.objects)
        if (object.pointer == parsed_main.exports.front().second) main_object = true;
    if (!main_object) throw std::runtime_error("main export 0 must identify a decoded object");
    auto first_export = static_cast<unsigned>(parsed_main.exports.size());
    if (first_export > 252) throw std::runtime_error("main export table cannot accommodate four byte-sized file-I/O bridges");
    std::size_t scripts = 0, remapped = 0, bridged = 0, sound_calls = 0, parser_calls = 0, opaque_exports = 0;
    bool sound_dispatch_checked = false;
    std::vector<std::string> legacy_branches;
    for (auto& entry : unique) if (entry.first.first == ResourceType::script) {
        ++scripts;
        auto& payload = entry.second.payload;
        const auto& parsed = script_metadata.at(entry.first.second);
        for (const auto& inst : parsed.instructions)
            if (inst.opcode >= 23 && inst.opcode <= 25 && !parsed.boundaries.count(target(inst)))
                legacy_branches.push_back("script " + std::to_string(entry.first.second) + " at byte " +
                    std::to_string(inst.start) + " -> " + std::to_string(target(inst)));
        for (const auto& exported : parsed.exports)
            if (exported.second && !parsed.boundaries.count(exported.second) && !parsed.object_pointers.count(exported.second)) ++opaque_exports;
        for (std::size_t instruction_index = 0; instruction_index < parsed.instructions.size(); ++instruction_index) {
            const auto& inst = parsed.instructions[instruction_index];
            if (inst.opcode != 33) continue;
            const auto kernel = inst.operands[0];
            if (kernel >= 0x24 && kernel <= 0x26) {
                const auto expected = kernel == 0x24 ? 4u : 2u;
                if (inst.operands.at(1) != expected)
                    throw std::runtime_error("unsupported parser migration argument frame in script " + std::to_string(entry.first.second));
                ++parser_calls;
            }
            if (kernel == 0x31) {
                if (!sound_dispatch_checked) { validate_sound_dispatch(game); sound_dispatch_checked = true; }
                validate_sound_call(parsed, instruction_index);
                ++sound_calls;
            }
            if (kernel > 0x71) throw std::runtime_error("unsupported SCI0 kernel ID in script " + std::to_string(entry.first.second));
            if (kernel == 0x55) throw std::runtime_error("SCI0 DoAvoider requires a semantic port; migration refused");
            if (kernel >= 0x29 && kernel <= 0x2c) {
                constexpr std::array<unsigned,4> counts{4,4,6,2};
                if (inst.operands[1] != counts[kernel-0x29]) throw std::runtime_error("unsupported SCI0 file-I/O argument count");
                payload[inst.start] = static_cast<std::uint8_t>((34u << 1) | (payload[inst.start] & 1u)); // callb bridge
                auto index = first_export+kernel-0x29;
                if (inst.width == 1) payload[inst.start+1] = static_cast<std::uint8_t>(index); else patch(payload,inst.start+1,index);
                ++bridged;
            } else if (kernel > 0x2c) {
                if (inst.width == 1) payload[inst.start+1] = static_cast<std::uint8_t>(kernel-4); else patch(payload,inst.start+1,kernel-4);
                ++remapped;
            }
        }
    }
    // Reparse after remapping so main-relative operands and references are based
    // on the actual modified bytes before expanding the export table.
    main->second.payload = add_file_bridges(main->second.payload, parse_migration_script(main->second), first_export);
    // Enforce the invariant that makes preserving legacy branches possible.
    // Kernel/bridge rewrites are equal length, and no branch operands change.
    for (const auto& entry : unique) if (entry.first.first == ResourceType::script && entry.first.second != 0) {
        const auto after = parse_migration_script(entry.second);
        const auto& before = script_metadata.at(entry.first.second);
        if (after.boundaries != before.boundaries || after.instructions.size() != before.instructions.size())
            throw std::runtime_error("migration changed non-main script instruction layout");
        for (std::size_t i = 0; i < before.instructions.size(); ++i) {
            const auto& old_inst = before.instructions[i];
            const auto& new_inst = after.instructions[i];
            if (old_inst.end != new_inst.end || (old_inst.opcode >= 23 && old_inst.opcode <= 25 &&
                (old_inst.opcode != new_inst.opcode || old_inst.operands != new_inst.operands)))
                throw std::runtime_error("migration changed a preserved branch operand or instruction extent");
        }
    }
    const auto format = parser_calls ? Sci1Format::sci1_ega : Sci1Format::sci1_late;
    if (parser_calls) adapt_parser(unique);
    std::vector<Resource> output;
    for (auto& entry : unique) output.push_back(std::move(entry.second));
    Sci1Migration result;
    result.files = write_sci1_game(output,format);
    const auto reopened = read_sci1_game(result.files,format);
    if (reopened.resources.size() != output.size()) throw std::runtime_error("migration resource count verification failed");
    for (std::size_t i = 0; i < output.size(); ++i)
        if (reopened.resources[i].entry.id.type != output[i].entry.id.type ||
            reopened.resources[i].entry.id.number != output[i].entry.id.number ||
            reopened.resources[i].payload != output[i].payload)
            throw std::runtime_error("migration container round-trip verification failed");
    std::ostringstream report;
    report << "SCIMATE experimental generic SCI0 -> SCI1 migration\n"
           << "Resources: " << output.size() << "; duplicate SCI0 IDs resolved using first map entry (ScummVM semantics).\n"
           << "Scripts scanned: " << scripts << "; remapped kernel calls: " << remapped << "; file-I/O bridge calls: " << bridged << ".\n"
           << (parser_calls ? "Target: SCI1 EGA packed-ID map, eight-byte volume headers, uncompressed payloads.\n" : "Target: SCI1 late directory map, nine-byte volume headers, uncompressed payloads.\n")
           << "Original EGA graphics, game-object name and relative LOFS semantics retained.\n"
           << "Parser calls retained: " << parser_calls << "; "
           << (parser_calls ? "word vocabulary 0 converted to 900; grammar 900 -> 901, suffixes 901 -> 902; obsolete SCI0 kernel names omitted.\n" : "no parser resource conversion required.\n")
           << "Main export expansion relocates absolute addresses; four FileIO bridges appended.\n"
           << "Validated SCI0 late DoSound calls: " << sound_calls << "; kernel remapped to 0x2d.\n"
           << (sound_calls ? "Sound resources retained after SCI0 late decoding; ScummVM late-sound dispatch verified by vocabulary 997.\n" : "No DoSound compatibility profile required.\n")
           << "Script layouts, main branches, methods and direct calls validated; opaque uncalled export slots preserved: " << opaque_exports << ".\n"
           << "Legacy non-main branch targets retained unchanged: " << legacy_branches.size() << "; runtime reachability UNVERIFIED.\n"
           << "Dynamic export accesses remain unverified; no game-specific assumptions, assets or detection hashes fabricated.\n"
           << "UNVERIFIED: runtime detection, graphics/sound compatibility, save/restore and gameplay require manual ScummVM validation.\n"
           << "This is a ScummVM-targeted compatibility candidate, not a claim of original Sierra interpreter compatibility or a general SCI1.1 migration.\n";
    for (const auto& branch : legacy_branches) report << "UNVERIFIED legacy branch: " << branch << ".\n";
    result.report = report.str();
    return result;
}
} // namespace scimate
