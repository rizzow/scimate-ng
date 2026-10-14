// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "script_internal.hpp"
#include "sci_vm.hpp"
#include "binary.hpp"

namespace scimate::migration_detail {
// Append bridge code and a replacement export table. SCI0/SCI1 select the last
// export table, so existing instructions, objects and relocation slots retain
// their addresses. This also avoids widening short relative instructions.
inline Bytes append_script_exports(const Bytes& original, const script_detail::ParsedScript& parsed,
                                  const std::vector<Bytes>& procedures) {
    if (procedures.empty()) return original;
    if (parsed.sections.empty() || parsed.exports.empty())
        throw std::runtime_error("migration bridge requires an existing export table");
    const auto word = [](Bytes& bytes, std::size_t value) {
        binary_detail::append(bytes,value,sci_vm::word_bytes,"migration bridge exceeds 16-bit script size");
    };
    Bytes bytes(original.begin(),original.begin()+parsed.sections.back().end);
    std::vector<unsigned> targets;
    for (const auto& entry : parsed.exports) targets.push_back(entry.second);
    const auto code_start = bytes.size();
    word(bytes,sci_vm::section::code); word(bytes,0);
    for (const auto& procedure : procedures) {
        if (procedure.empty()) throw std::runtime_error("migration bridge has no instructions");
        targets.push_back(static_cast<unsigned>(bytes.size()));
        bytes.insert(bytes.end(),procedure.begin(),procedure.end());
    }
    binary_detail::patch_word(bytes,code_start+2,bytes.size()-code_start,"migration bridge code exceeds section size");
    word(bytes,sci_vm::section::exports);
    word(bytes,sci_vm::section_header_bytes+sci_vm::word_bytes*(targets.size()+1));
    word(bytes,targets.size());
    for (const auto target : targets) word(bytes,target);
    word(bytes,0);
    if (bytes.size() > sci_vm::max_script_address)
        throw std::runtime_error("migration bridges exceed script address space");
    const auto after = script_detail::parse(bytes,true,false);
    if (after.exports.size() != targets.size())
        throw std::runtime_error("migration bridge export table verification failed");
    return bytes;
}
} // namespace scimate::migration_detail
