// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "script_internal.hpp"
#include "binary.hpp"
#include "sci_vm.hpp"
#include <algorithm>

namespace scimate::migration_detail {
// Only main export 0 selects the game object. Keep old strings and every source
// address intact; no title name or resource-specific identity rule is needed.
inline Bytes rename_main_game_object(const Bytes& original, const std::string& name) {
    if (name.empty() || name.size() > 255 ||
        std::any_of(name.begin(),name.end(),[](unsigned char ch) { return ch < 32 || ch > 126; }))
        throw std::runtime_error("migration identity requires a nonempty printable ASCII name of at most 255 bytes");
    const auto parsed = script_detail::parse(original,true,false);
    if (parsed.exports.empty()) throw std::runtime_error("migration identity requires main export 0");
    const auto object = std::find_if(parsed.objects.begin(),parsed.objects.end(),[&](const auto& candidate) {
        return candidate.pointer == parsed.exports.front().second;
    });
    constexpr unsigned name_property = 3;
    if (object == parsed.objects.end() || object->values.size() <= name_property ||
        !parsed.strings.count(object->values[name_property]))
        throw std::runtime_error("migration identity requires a game object with a valid name string");
    const auto slot = object->pointer+name_property*sci_vm::word_bytes;
    bool relocated = false;
    for (const auto& section : parsed.sections) if (section.type == sci_vm::section::relocations) {
        const auto count = script_detail::word(original,section.start+sci_vm::section_header_bytes,section.end);
        for (unsigned i = 0; i < count; ++i)
            relocated |= script_detail::word(original,section.start+sci_vm::section_header_bytes+sci_vm::word_bytes*(i+1),section.end) == slot;
        break; // The interpreter uses the first relocation block.
    }
    if (!relocated) throw std::runtime_error("migration identity requires the game name property in the active relocation table");
    Bytes bytes(original.begin(),original.begin()+parsed.sections.back().end);
    const auto string_offset = bytes.size()+sci_vm::section_header_bytes;
    const auto string_bytes = (name.size()+2)&~std::size_t(1); // NUL plus word alignment.
    const auto append_word = [&](std::size_t value) {
        binary_detail::append(bytes,value,sci_vm::word_bytes,"migration identity exceeds script address space");
    };
    append_word(sci_vm::section::strings); append_word(sci_vm::section_header_bytes+string_bytes);
    bytes.insert(bytes.end(),name.begin(),name.end()); bytes.resize(string_offset+string_bytes,0);
    append_word(0);
    if (bytes.size() > sci_vm::max_script_address)
        throw std::runtime_error("migration identity exceeds script address space");
    binary_detail::patch_word(bytes,slot,string_offset,"migration identity exceeds script address space");
    script_detail::parse(bytes,true,false);
    return bytes;
}
} // namespace scimate::migration_detail
