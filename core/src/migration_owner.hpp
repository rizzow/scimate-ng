// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "script_internal.hpp"
#include "sci_vm.hpp"
#include "binary.hpp"
#include <algorithm>

namespace scimate::migration_detail {
struct AdditionalMethod { unsigned selector, address; };
using AdditionalMethods = std::map<std::size_t,std::vector<AdditionalMethod>>;
// Append a constant owner query to each object's dispatch table, preserving
// property layouts and all source addresses. Tables are binary NUL-terminated
// strings in a new strings block; their word encoding is read by object dispatch.
// Clones retain the source dispatch table and therefore the same owner query.
inline Bytes append_script_owner_method(const Bytes& original, unsigned script, unsigned selector,
                                       const AdditionalMethods& extra = {}) {
    if (script > sci_vm::maximum_packed_resource_id || selector > sci_vm::max_script_address)
        throw std::runtime_error("migration owner method exceeds VM operand limits");
    const auto parsed = script_detail::parse(original,false,false);
    if (parsed.objects.empty()) return original;
    for (const auto& object : parsed.objects)
        for (const auto* selectors : {&object.methods,&object.selectors})
            if (std::find(selectors->begin(),selectors->end(),selector) != selectors->end())
                throw std::runtime_error("migration owner selector collides with a source selector");
    for (const auto& addition : extra) {
        const auto object = std::find_if(parsed.objects.begin(),parsed.objects.end(),[&](const auto& candidate) {
            return candidate.pointer == addition.first;
        });
        if (object == parsed.objects.end()) throw std::runtime_error("migration method has no source object");
        std::set<unsigned> methods{selector};
        methods.insert(object->methods.begin(),object->methods.end());
        methods.insert(object->selectors.begin(),object->selectors.end());
        for (const auto& method : addition.second)
            if (method.selector > sci_vm::max_script_address || !methods.insert(method.selector).second ||
                !parsed.boundaries.count(method.address))
                throw std::runtime_error("migration method has a colliding selector or invalid code target");
    }
    const auto append_start = parsed.sections.back().end;
    Bytes bytes(original.begin(),original.begin()+append_start);
    const auto word = [&](std::size_t value) {
        binary_detail::append(bytes,value,sci_vm::word_bytes,"migration owner method exceeds script address space");
    };
    word(sci_vm::section::code); word(sci_vm::section_header_bytes+4);
    const auto entry = bytes.size();
    bytes.push_back(static_cast<std::uint8_t>(sci_vm::encoded_opcode(sci_vm::opcode::load_immediate)));
    word(script); bytes.push_back(static_cast<std::uint8_t>(sci_vm::encoded_opcode(sci_vm::opcode::return_)));
    const auto strings = bytes.size();
    word(sci_vm::section::strings); word(0);
    for (const auto& object : parsed.objects) {
        const auto added = extra.find(object.pointer);
        const auto count = added == extra.end() ? 0 : added->second.size();
        const auto table = bytes.size();
        word(object.methods.size()+count+1);
        for (const auto method : object.methods) word(method);
        if (added != extra.end()) for (const auto& method : added->second) word(method.selector);
        word(selector); word(0);
        for (const auto address : object.addresses) word(address);
        if (added != extra.end()) for (const auto& method : added->second) word(method.address);
        word(entry);
        binary_detail::patch_word(bytes,object.pointer-4,table-object.pointer+2,
            "migration owner method table exceeds header offset");
    }
    word(0); // Terminate any final binary string and align the section.
    binary_detail::patch_word(bytes,strings+2,bytes.size()-strings,"migration owner tables exceed section size");
    word(0);
    if (bytes.size() > sci_vm::max_script_address)
        throw std::runtime_error("migration owner method exceeds script address space");
    // Appended code/data must not give an old invalid reference a new meaning.
    const auto enters_append = [&](unsigned target) { return target >= append_start && target < bytes.size(); };
    for (const auto& instruction : parsed.instructions)
        if (script_detail::relative_operand(instruction.opcode) && enters_append(script_detail::target(instruction)))
            throw std::runtime_error("migration owner method would expose appended data to a source relative reference");
    for (const auto& exported : parsed.exports)
        if (enters_append(exported.second))
            throw std::runtime_error("migration owner method would give an opaque source export a new target");
    const auto after = script_detail::parse(bytes,false,false);
    for (std::size_t i = 0; i < parsed.objects.size(); ++i) {
        const auto& old = parsed.objects[i]; const auto& added = after.objects.at(i);
        if (old.pointer != added.pointer || old.values != added.values || old.selectors != added.selectors ||
            added.methods.back() != selector || added.addresses.back() != entry)
            throw std::runtime_error("migration owner method changed a source object layout");
    }
    return bytes;
}
} // namespace scimate::migration_detail
