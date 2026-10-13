// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "script_internal.hpp"
#include "sci_vm.hpp"
#include <map>
#include <set>
#include <tuple>

namespace scimate::migration_detail {
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
