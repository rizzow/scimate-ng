// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "script_internal.hpp"
#include "sci_vm.hpp"
#include "binary.hpp"

namespace scimate::migration_detail {
// Resolve the main object before &rest is active, then enter an injected game
// method. SEND changes the VM receiver; CALLB/CALLE alone inherit the caller's.
inline Bytes forward_game_method(unsigned selector) {
    if (selector > sci_vm::max_script_address) throw std::runtime_error("migration game selector exceeds VM operand");
    namespace op = sci_vm::opcode;
    const auto byte_instruction = [](Bytes& code, unsigned opcode, unsigned operand) {
        code.push_back(static_cast<std::uint8_t>(sci_vm::encoded_opcode(opcode,true)));
        code.push_back(static_cast<std::uint8_t>(operand));
    };
    Bytes code;
    byte_instruction(code,op::allocate_temporaries,1);
    for (const auto opcode : {op::push_two,op::push_zero,op::push_zero})
        code.push_back(static_cast<std::uint8_t>(sci_vm::encoded_opcode(opcode)));
    byte_instruction(code,op::call_kernel,sci_vm::sci1_kernel::script_id);
    code.push_back(2*sci_vm::word_bytes);
    byte_instruction(code,op::store_accumulator_to_temporary,0);
    code.push_back(static_cast<std::uint8_t>(sci_vm::encoded_opcode(op::push_immediate)));
    binary_detail::append(code,selector,sci_vm::word_bytes,"migration game selector exceeds VM operand");
    code.push_back(static_cast<std::uint8_t>(sci_vm::encoded_opcode(op::push_zero)));
    byte_instruction(code,op::rest,1);
    byte_instruction(code,op::load_temporary_to_accumulator,0);
    byte_instruction(code,op::send,2*sci_vm::word_bytes);
    code.push_back(static_cast<std::uint8_t>(sci_vm::encoded_opcode(op::return_)));
    return code;
}
// Forward the full actual argument list to a generated runtime export. The
// zero fixed frame plus &rest preserves optional/runtime-supplied parameters.
inline Bytes forward_script_export(unsigned script, unsigned exported) {
    if (script > sci_vm::maximum_packed_resource_id || exported > sci_vm::max_script_address)
        throw std::runtime_error("migration bridge destination exceeds SCI1 format limits");
    Bytes code{static_cast<std::uint8_t>(sci_vm::encoded_opcode(sci_vm::opcode::push_zero)),
        static_cast<std::uint8_t>(sci_vm::encoded_opcode(sci_vm::opcode::rest,true)),1,
        static_cast<std::uint8_t>(sci_vm::encoded_opcode(sci_vm::opcode::call_external))};
    for (const auto value : {script,exported})
        binary_detail::append(code,value,sci_vm::word_bytes,"migration bridge destination exceeds VM operands");
    code.push_back(0); // Fixed argument frame; &rest contributes the actual list.
    code.push_back(static_cast<std::uint8_t>(sci_vm::encoded_opcode(sci_vm::opcode::return_)));
    return code;
}
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
