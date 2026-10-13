// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "script_internal.hpp"
#include "sci_vm.hpp"
#include <map>
#include <array>
#include <optional>
#include <set>
#include <tuple>

namespace scimate::migration_detail {
struct VolumeQueryFrame {
    const script_detail::Instruction& count;
    const script_detail::Instruction& parameter;
    const script_detail::Instruction& call;
};
struct InstructionPattern {
    unsigned opcode;
    std::vector<std::optional<unsigned>> operands; // nullopt accepts any operand.
};
inline bool matches(const script_detail::Instruction& instruction, const InstructionPattern& pattern) {
    if (instruction.opcode != pattern.opcode) return false;
    if (instruction.operands.size() != pattern.operands.size()) return false;
    for (std::size_t i = 0; i < pattern.operands.size(); ++i) {
        const auto expected = pattern.operands[i];
        if (expected && instruction.operands[i] != *expected) return false;
    }
    return true;
}
inline std::optional<VolumeQueryFrame> volume_query_frame(const script_detail::ParsedScript& script,
                                                        std::size_t call_index) {
    namespace op = sci_vm::opcode;
    constexpr unsigned master_volume_command = 8;
    constexpr unsigned setter_frame_bytes = 2*sci_vm::word_bytes;
    static const std::array<InstructionPattern,4> pattern{{
        {op::push_two,{}},
        {op::push_immediate,{master_volume_command}},
        {op::load_temporary_to_stack,{std::nullopt}},
        {op::call_kernel,{sci_vm::sci0_kernel::do_sound,setter_frame_bytes}}
    }};
    if (call_index < pattern.size()-1 || call_index >= script.instructions.size()) return std::nullopt;
    const auto first = call_index-(pattern.size()-1);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const auto& instruction = script.instructions[first+i];
        if (!matches(instruction,pattern[i])) return std::nullopt;
        if (i == 0) continue;
        if (script.instructions[first+i-1].end != instruction.start) return std::nullopt;
        if (script.labels.count(instruction.start)) return std::nullopt;
        if (script.procedures.count(instruction.start)) return std::nullopt;
    }
    return VolumeQueryFrame{script.instructions[first],script.instructions[call_index-1],script.instructions[call_index]};
}
// Definite-assignment analysis for a temporary slot. A direct/indexed write or
// an address exposing this slot prevents a query repair; paths are not guessed.
inline bool uninitialized_temporary(const script_detail::ParsedScript& script,
                                     unsigned slot, std::size_t read_address) {
    namespace op = sci_vm::opcode;
    using State = std::tuple<std::size_t,bool,bool>;
    std::map<std::size_t,const script_detail::Instruction*> instructions;
    for (const auto& instruction : script.instructions) instructions.emplace(instruction.start,&instruction);
    std::vector<State> work;
    for (const auto root : script.procedures) work.emplace_back(root,false,false);
    std::set<State> seen;
    bool reaches_read = false;
    while (!work.empty()) {
        const auto state = work.back(); work.pop_back();
        if (!seen.insert(state).second) continue;
        const auto [address,allocated,written] = state;
        if (address == read_address) {
            if (!allocated || written) return false;
            reaches_read = true;
            continue;
        }
        const auto found = instructions.find(address);
        if (found == instructions.end()) continue;
        const auto& instruction = *found->second;
        bool next_allocated = allocated, next_written = written;
        if (instruction.opcode == op::allocate_temporaries) {
            next_allocated = instruction.operands.at(0) > slot;
            next_written = false;
        }
        if (op::writes_temporary(instruction.opcode) &&
            ((instruction.opcode & op::indexed_variable_flag) || instruction.operands.at(0) == slot)) next_written = true;
        if (instruction.opcode == op::load_address &&
            ((instruction.operands.at(0) >> op::address_storage_shift) & op::variable_storage_mask) == op::temporary_storage) next_written = true;
        if (op::is_branch(instruction.opcode))
            work.emplace_back(script_detail::target(instruction),next_allocated,next_written);
        if (instruction.opcode != op::jump && instruction.opcode != op::return_)
            work.emplace_back(instruction.end,next_allocated,next_written);
    }
    return reaches_read;
}
}
