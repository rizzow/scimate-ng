// SPDX-License-Identifier: GPL-2.0-or-later
// Compatibility boundaries derived from ScummVM kernel/features/script sources.
#include "scimate/sci1.hpp"
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
    auto parsed_main = parse(main->second.payload);
    if (parsed_main.exports.empty()) throw std::runtime_error("migration requires main export 0");
    bool main_object = false;
    for (const auto& object : parsed_main.objects)
        if (object.pointer == parsed_main.exports.front().second) main_object = true;
    if (!main_object) throw std::runtime_error("main export 0 must identify a decoded object");
    auto first_export = static_cast<unsigned>(parsed_main.exports.size());
    if (first_export > 252) throw std::runtime_error("main export table cannot accommodate four byte-sized file-I/O bridges");
    std::size_t scripts = 0, remapped = 0, bridged = 0;
    for (auto& entry : unique) if (entry.first.first == ResourceType::script) {
        ++scripts;
        auto& payload = entry.second.payload;
        auto parsed = parse(payload);
        for (const auto& inst : parsed.instructions) if (inst.opcode == 33) {
            const auto kernel = inst.operands[0];
            if (kernel == 0x24 || kernel == 0x25)
                throw std::runtime_error("parser migration requires a generic SCI1 parser adaptation; no game-ID compatibility exceptions are permitted");
            if (kernel == 0x31)
                throw std::runtime_error("SCI0 DoSound requires sound script/resource adaptation; compatibility cannot be inferred from game identity or selector names");
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
    main->second.payload = add_file_bridges(main->second.payload, parse(main->second.payload), first_export);
    std::vector<Resource> output;
    for (auto& entry : unique) output.push_back(std::move(entry.second));
    Sci1Migration result;
    result.files = write_sci1_game(output,Sci1Format::sci1_late);
    const auto reopened = read_sci1_game(result.files,Sci1Format::sci1_late);
    if (reopened.resources.size() != output.size()) throw std::runtime_error("migration resource count verification failed");
    for (std::size_t i = 0; i < output.size(); ++i)
        if (reopened.resources[i].entry.id.type != output[i].entry.id.type ||
            reopened.resources[i].entry.id.number != output[i].entry.id.number ||
            reopened.resources[i].payload != output[i].payload)
            throw std::runtime_error("migration container round-trip verification failed");
    std::ostringstream report;
    report << "SCIMATE experimental generic SCI0 -> SCI1 late migration\n"
           << "Resources: " << output.size() << "; duplicate SCI0 IDs resolved using first map entry (ScummVM semantics).\n"
           << "Scripts scanned: " << scripts << "; remapped kernel calls: " << remapped << "; file-I/O bridge calls: " << bridged << ".\n"
           << "Target: SCI1 late directory map, nine-byte volume headers, uncompressed payloads.\n"
           << "Original EGA graphics, parser vocabulary, game-object name and relative LOFS semantics retained.\n"
           << "Main export expansion relocates absolute addresses; four FileIO bridges appended.\n"
           << "Source scripts strictly validated; no game-specific assumptions, assets or detection hashes fabricated.\n"
           << "UNVERIFIED: runtime detection, graphics/sound compatibility, save/restore and gameplay require manual ScummVM validation.\n"
           << "This is a ScummVM-targeted compatibility candidate, not a claim of original Sierra interpreter compatibility or a general SCI1.1 migration.\n";
    result.report = report.str();
    return result;
}
} // namespace scimate
