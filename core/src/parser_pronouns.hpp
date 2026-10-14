// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_scanner.hpp"
#include "parser_tree.hpp"
#include "parser_said.hpp"

namespace scimate::parser_detail {
inline constexpr unsigned no_pronoun_reference = 0x1000, pronoun_class_mask = 0x80, noun_class = 0x100;
struct PronounState { unsigned reference; ScriptBuilder::Label replace, remember; };
inline PronounState emit_pronoun_state(ScriptBuilder& builder, const TokenScanner& tokens) {
    namespace op = sci_vm::opcode;
    using namespace codegen;
    const auto reference = builder.local(no_pronoun_reference);
    const auto scan_major = builder.label(), replace = builder.label(), remember = builder.label();
    const auto field = [](Value arena, Value node, unsigned member) {
        return candidate_word(arena,binary(sci_vm::opcode::add,node,number(member)));
    };
    {
        // (arena, parent node, requested major): prefer an immediate child;
        // only if absent descend through the first generic child, as SCI does.
        constexpr unsigned child = 0;
        const auto terminal = builder.label(), loop = builder.label(), next = builder.label(), missing = builder.label(), found = builder.label();
        builder.bind(scan_major); builder.emit(op::allocate_temporaries,{1});
        branch_if(builder,parameter(2),missing,false);
        branch_if(builder,field(parameter(1),parameter(2),tree_field::terminal),terminal);
        set_temporary(builder,child,field(parameter(1),parameter(2),tree_field::child));
        builder.bind(loop); branch_if(builder,temporary(child),next,false);
        branch_if(builder,binary(op::equal,field(parameter(1),temporary(child),tree_field::major),parameter(3)),found);
        set_temporary(builder,child,field(parameter(1),temporary(child),tree_field::sibling)); builder.branch(op::jump,loop);
        builder.bind(next); branch_if(builder,binary(op::equal,parameter(3),number(said_tag::generic)),missing);
        set_temporary(builder,child,procedure(scan_major,{parameter(1),parameter(2),number(said_tag::generic)}));
        branch_if(builder,temporary(child),missing,false);
        return_value(builder,procedure(scan_major,{parameter(1),temporary(child),parameter(3)}));
        builder.bind(terminal); branch_if(builder,binary(op::equal,field(parameter(1),parameter(2),tree_field::major),parameter(3)),missing,false);
        return_value(builder,parameter(2));
        builder.bind(found); return_value(builder,temporary(child)); builder.bind(missing); return_value(builder,number(0));
    }
    {
        constexpr unsigned cursor = 0;
        const auto loop = builder.label(), next = builder.label(), done = builder.label();
        builder.bind(replace); builder.emit(op::allocate_temporaries,{1});
        branch_if(builder,binary(op::equal,local(reference),number(no_pronoun_reference)),done);
        set_temporary(builder,cursor,number(0)); builder.bind(loop);
        branch_if(builder,binary(op::less_unsigned,temporary(cursor),local(tokens.count)),done,false);
        branch_if(builder,binary(op::bitwise_and,indexed_local(tokens.classes,temporary(cursor)),number(pronoun_class_mask)),next,false);
        set_indexed_local(builder,tokens.classes,temporary(cursor),number(noun_class));
        set_indexed_local(builder,tokens.groups,temporary(cursor),local(reference));
        builder.bind(next); builder.emit(op::increment_temporary_to_accumulator,{cursor}); builder.branch(op::jump,loop);
        builder.bind(done); return_value(builder,number(0));
    }
    {
        constexpr unsigned node = 0;
        const auto loop = builder.label(), found = builder.label(), done = builder.label();
        builder.bind(remember); builder.emit(op::allocate_temporaries,{1});
        set_temporary(builder,node,procedure(scan_major,{parameter(1),number(tree_root_index),number(said_tag::subject)}));
        builder.bind(loop); branch_if(builder,temporary(node),done,false);
        branch_if(builder,field(parameter(1),temporary(node),tree_field::terminal),found);
        set_temporary(builder,node,procedure(scan_major,{parameter(1),temporary(node),number(said_tag::generic)}));
        builder.branch(op::jump,loop);
        builder.bind(found); set_local(builder,reference,field(parameter(1),temporary(node),tree_field::word));
        return_value(builder,number(1)); builder.bind(done); return_value(builder,number(0));
    }
    return {reference,replace,remember};
}
} // namespace scimate::parser_detail
