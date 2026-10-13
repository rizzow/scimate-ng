// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_codegen.hpp"
#include "parser_grammar.hpp"

namespace scimate::parser_detail {
// A candidate is a dynamic SCI memory block: one length word followed by
// kind/value pairs. Every pointer stays in a VM register, never in raw bytes.
inline constexpr unsigned candidate_symbol_limit = 1024, candidate_header_words = 1, candidate_symbol_words = 2;
inline codegen::Value candidate_word(codegen::Value pointer, codegen::Value word_index) {
    using namespace codegen;
    return kernel(sci_vm::sci1_kernel::memory,{number(sci_vm::memory_operation::peek),
        binary(sci_vm::opcode::add,pointer,binary(sci_vm::opcode::multiply,word_index,number(sci_vm::word_bytes)))});
}
inline void set_candidate_word(ScriptBuilder& builder, codegen::Value pointer, codegen::Value word_index, codegen::Value value) {
    using namespace codegen;
    kernel(sci_vm::sci1_kernel::memory,{number(sci_vm::memory_operation::poke),
        binary(sci_vm::opcode::add,pointer,binary(sci_vm::opcode::multiply,word_index,number(sci_vm::word_bytes))),value})(builder);
}
struct CandidateProcedures { ScriptBuilder::Label find_pending, consume_word, splice_rule; };
inline CandidateProcedures emit_candidate_procedures(ScriptBuilder& builder, const CompiledGrammarLayout& grammar) {
    namespace op = sci_vm::opcode;
    using namespace codegen;
    using Kind = GrammarSymbolKind;
    const auto find = builder.label(), consume = builder.label(), splice = builder.label();
    // find_pending(pointer, nonterminal_only): one-based symbol index or zero.
    {
        constexpr unsigned cursor = 0, length = 1, kind = 2;
        const auto scan = builder.label(), advance = builder.label(), found = builder.label(), missing = builder.label();
        builder.bind(find); builder.emit(op::allocate_temporaries,{3});
        branch_if(builder,parameter(1),missing,false);
        set_temporary(builder,length,candidate_word(parameter(1),number(0))); set_temporary(builder,cursor,number(0));
        builder.bind(scan);
        branch_if(builder,binary(op::less_unsigned,temporary(cursor),temporary(length)),missing,false);
        const auto position = binary(op::add,number(candidate_header_words),binary(op::multiply,temporary(cursor),number(candidate_symbol_words)));
        set_temporary(builder,kind,candidate_word(parameter(1),position));
        branch_if(builder,binary(op::less_unsigned,temporary(kind),number(static_cast<unsigned>(Kind::open))),advance,false);
        branch_if(builder,parameter(2),found,false);
        branch_if(builder,binary(op::equal,temporary(kind),number(static_cast<unsigned>(Kind::nonterminal))),found);
        builder.bind(advance); builder.emit(op::increment_temporary_to_accumulator,{cursor}); builder.branch(op::jump,scan);
        builder.bind(found); return_value(builder,binary(op::add,temporary(cursor),number(1)));
        builder.bind(missing); return_value(builder,number(0));
    }
    // consume_word(pointer, class, group): replace a matching terminal in place.
    {
        constexpr unsigned position = 0, kind = 1, mask = 2;
        const auto group = builder.label(), compare = builder.label(), missing = builder.label();
        builder.bind(consume); builder.emit(op::allocate_temporaries,{3});
        set_temporary(builder,position,procedure(find,{parameter(1),number(0)})); branch_if(builder,temporary(position),missing,false);
        set_temporary(builder,position,binary(op::add,number(candidate_header_words),
            binary(op::multiply,binary(op::subtract,temporary(position),number(1)),number(candidate_symbol_words))));
        set_temporary(builder,kind,candidate_word(parameter(1),temporary(position)));
        set_temporary(builder,mask,candidate_word(parameter(1),binary(op::add,temporary(position),number(1))));
        branch_if(builder,binary(op::equal,temporary(kind),number(static_cast<unsigned>(Kind::class_mask))),group,false);
        parameter(2)(builder); builder.branch(op::jump,compare);
        builder.bind(group); branch_if(builder,binary(op::equal,temporary(kind),number(static_cast<unsigned>(Kind::group_mask))),missing,false);
        parameter(3)(builder);
        builder.bind(compare); builder.emit(op::push_accumulator); temporary(mask)(builder); builder.emit(op::bitwise_and);
        builder.branch(op::branch_false,missing);
        set_candidate_word(builder,parameter(1),temporary(position),number(static_cast<unsigned>(Kind::word)));
        set_candidate_word(builder,parameter(1),binary(op::add,temporary(position),number(1)),parameter(3));
        return_value(builder,number(1)); builder.bind(missing); return_value(builder,number(0));
    }
    // splice_rule(pointer-or-zero, one-based rule): allocate an independent
    // candidate, retaining the prefix/suffix around its first nonterminal.
    {
        constexpr unsigned rule = 0, rule_length = 1, old_length = 2, before = 3, new_length = 4;
        constexpr unsigned output = 5, cursor = 6, value = 7;
        const auto initial = builder.label(), ready = builder.label(), scan = builder.label(), old_prefix = builder.label();
        const auto old_suffix = builder.label(), write = builder.label(), done = builder.label(), missing = builder.label();
        builder.bind(splice); builder.emit(op::allocate_temporaries,{8});
        branch_if(builder,parameter(2),missing,false);
        branch_if(builder,binary(op::less_unsigned,parameter(2),binary(op::add,local(grammar.count_local),number(1))),missing,false);
        set_temporary(builder,rule,indexed_local(grammar.offsets_local,binary(op::subtract,parameter(2),number(1))));
        set_temporary(builder,rule_length,indexed_local(1,temporary(rule)));
        branch_if(builder,parameter(1),initial,false);
        set_temporary(builder,old_length,candidate_word(parameter(1),number(0)));
        set_temporary(builder,before,procedure(find,{parameter(1),number(1)})); branch_if(builder,temporary(before),missing,false);
        set_temporary(builder,before,binary(op::subtract,temporary(before),number(1)));
        const auto old_value = binary(op::add,number(candidate_header_words+1),binary(op::multiply,temporary(before),number(candidate_symbol_words)));
        branch_if(builder,binary(op::equal,candidate_word(parameter(1),old_value),indexed_local(0,temporary(rule))),missing,false);
        set_temporary(builder,new_length,binary(op::subtract,binary(op::add,temporary(old_length),temporary(rule_length)),number(1)));
        builder.branch(op::jump,ready);
        builder.bind(initial); set_temporary(builder,old_length,number(0)); set_temporary(builder,before,number(0));
        set_temporary(builder,new_length,temporary(rule_length));
        builder.bind(ready);
        branch_if(builder,binary(op::less_unsigned,temporary(new_length),number(candidate_symbol_limit+1)),missing,false);
        const auto size = binary(op::multiply,binary(op::add,number(candidate_header_words),
            binary(op::multiply,temporary(new_length),number(candidate_symbol_words))),number(sci_vm::word_bytes));
        set_temporary(builder,output,kernel(sci_vm::sci1_kernel::memory,{number(sci_vm::memory_operation::allocate),size}));
        branch_if(builder,temporary(output),missing,false);
        set_candidate_word(builder,temporary(output),number(0),temporary(new_length)); set_temporary(builder,cursor,number(0));
        builder.bind(scan);
        const auto before_words = binary(op::multiply,temporary(before),number(candidate_symbol_words));
        const auto rule_words = binary(op::multiply,temporary(rule_length),number(candidate_symbol_words));
        branch_if(builder,binary(op::less_unsigned,temporary(cursor),binary(op::multiply,temporary(new_length),number(candidate_symbol_words))),done,false);
        branch_if(builder,binary(op::less_unsigned,temporary(cursor),before_words),old_prefix);
        branch_if(builder,binary(op::less_unsigned,temporary(cursor),binary(op::add,before_words,rule_words)),old_suffix,false);
        set_temporary(builder,value,indexed_local(3,binary(op::add,temporary(rule),binary(op::subtract,temporary(cursor),before_words))));
        builder.branch(op::jump,write);
        builder.bind(old_prefix); set_temporary(builder,value,candidate_word(parameter(1),binary(op::add,number(candidate_header_words),temporary(cursor))));
        builder.branch(op::jump,write);
        builder.bind(old_suffix); set_temporary(builder,value,candidate_word(parameter(1),binary(op::add,number(candidate_header_words+candidate_symbol_words),
            binary(op::subtract,temporary(cursor),rule_words))));
        builder.bind(write); set_candidate_word(builder,temporary(output),binary(op::add,number(candidate_header_words),temporary(cursor)),temporary(value));
        builder.emit(op::increment_temporary_to_accumulator,{cursor}); builder.branch(op::jump,scan);
        builder.bind(done); return_value(builder,temporary(output)); builder.bind(missing); return_value(builder,number(0));
    }
    return {find,consume,splice};
}
} // namespace scimate::parser_detail
