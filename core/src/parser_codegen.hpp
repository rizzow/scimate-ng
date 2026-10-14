// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_bytecode.hpp"
#include <functional>

namespace scimate::parser_detail::codegen {
using Value = std::function<void(ScriptBuilder&)>;
inline Value instruction(unsigned opcode, std::vector<unsigned> operands) {
    return [=](ScriptBuilder& builder) {
        if (operands.empty()) builder.emit(opcode);
        else if (operands.size() == 1) builder.emit(opcode,{operands[0]});
        else if (operands.size() == 2) builder.emit(opcode,{operands[0],operands[1]});
        else throw std::runtime_error("portable parser expression has too many operands");
    };
}
inline Value number(unsigned value) { return instruction(sci_vm::opcode::load_immediate,{value}); }
inline Value temporary(unsigned index) { return instruction(sci_vm::opcode::load_temporary_to_accumulator,{index}); }
inline Value parameter(unsigned index) { return instruction(sci_vm::opcode::load_parameter_to_accumulator,{index}); }
inline Value local(unsigned index) { return instruction(sci_vm::opcode::load_local_to_accumulator,{index}); }
inline Value address(unsigned index) {
    constexpr unsigned local_address_mode = 2;
    return instruction(sci_vm::opcode::load_address,{local_address_mode,index});
}
inline Value binary(unsigned opcode, Value left, Value right) {
    return [=](ScriptBuilder& builder) {
        left(builder); builder.emit(sci_vm::opcode::push_accumulator); right(builder); builder.emit(opcode);
    };
}
inline Value indexed_local(unsigned base, Value index) {
    return [=](ScriptBuilder& builder) { index(builder); builder.emit(sci_vm::opcode::load_indexed_local_to_accumulator,{base}); };
}
inline void arguments(ScriptBuilder& builder, const std::vector<Value>& values) {
    builder.emit(sci_vm::opcode::push_immediate,{static_cast<unsigned>(values.size())});
    for (const auto& value : values) { value(builder); builder.emit(sci_vm::opcode::push_accumulator); }
}
inline Value kernel(unsigned id, std::vector<Value> values) {
    return [=](ScriptBuilder& builder) {
        arguments(builder,values); builder.emit(sci_vm::opcode::call_kernel,{id,static_cast<unsigned>(values.size()*sci_vm::word_bytes)});
    };
}
inline Value procedure(ScriptBuilder::Label label, std::vector<Value> values) {
    return [=](ScriptBuilder& builder) { arguments(builder,values); builder.call(label,static_cast<unsigned>(values.size())); };
}
inline Value external_procedure(unsigned script, unsigned slot, std::vector<Value> values) {
    return [=](ScriptBuilder& builder) {
        arguments(builder,values);
        builder.emit(sci_vm::opcode::call_external,{script,slot,static_cast<unsigned>(values.size()*sci_vm::word_bytes)});
    };
}
inline Value send(Value object, unsigned selector, std::vector<Value> values = {}) {
    return [=](ScriptBuilder& builder) {
        builder.emit(sci_vm::opcode::push_immediate,{selector}); arguments(builder,values);
        object(builder); builder.emit(sci_vm::opcode::send,{static_cast<unsigned>((values.size()+2)*sci_vm::word_bytes)});
    };
}
inline void set_temporary(ScriptBuilder& builder, unsigned index, const Value& value) {
    value(builder); builder.emit(sci_vm::opcode::store_accumulator_to_temporary,{index});
}
inline void set_local(ScriptBuilder& builder, unsigned index, const Value& value) {
    value(builder); builder.emit(sci_vm::opcode::store_accumulator_to_local,{index});
}
inline void set_indexed_local(ScriptBuilder& builder, unsigned base, const Value& index, const Value& value) {
    value(builder); builder.emit(sci_vm::opcode::push_accumulator); index(builder);
    builder.emit(sci_vm::opcode::store_stack_to_indexed_local,{base});
}
inline void branch_if(ScriptBuilder& builder, const Value& condition, ScriptBuilder::Label target, bool when_true = true) {
    condition(builder); builder.branch(when_true ? sci_vm::opcode::branch_true : sci_vm::opcode::branch_false,target);
}
inline void return_value(ScriptBuilder& builder, const Value& value) { value(builder); builder.emit(sci_vm::opcode::return_); }
} // namespace scimate::parser_detail::codegen
