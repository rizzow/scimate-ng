// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_candidates.hpp"
#include "parser_tree.hpp"
#include "parser_said.hpp"

namespace scimate::parser_detail {
inline unsigned emit_said_tree(ScriptBuilder& builder, const SaidNode& tree) {
    builder.local(); // Zero is the null-node index.
    const std::function<unsigned(const SaidNode&)> append = [&](const SaidNode& node) {
        const auto start = builder.local(node.major); builder.local(node.minor);
        builder.local(node.word.has_value()); builder.local(node.word.value_or(0)); builder.local(); builder.local();
        unsigned previous = 0;
        for (const auto& child : node.children) {
            const auto index = append(child);
            builder.set_local(previous ? previous+tree_field::sibling : start+tree_field::child,index);
            previous = index;
        }
        return start;
    };
    return append(tree);
}
inline constexpr unsigned tree_absent = 0, tree_matched = 1, tree_mismatched = 0xffff;
struct SaidMatcher { ScriptBuilder::Label procedure; unsigned partial; };
// ABI: (parsed arena pointer, parsed node word-index, Said node local-index).
// Parsed and Said trees share the six-word record shape, but pointers are kept
// separate: the arena is dynamic memory; the Said tree is immutable script data.
inline SaidMatcher emit_said_matcher(ScriptBuilder& builder) {
    namespace op = sci_vm::opcode;
    using namespace codegen;
    const auto partial = builder.local();
    const auto match = builder.label(), scan_parse = builder.label(), scan_said = builder.label(), entry = builder.label();
    const auto parsed = [&](unsigned field) { return candidate_word(parameter(1),binary(op::add,parameter(2),number(field))); };
    const auto said = [&](unsigned field) { return indexed_local(field,parameter(3)); };
    const auto alternatives = binary(op::bitwise_or,
        binary(op::equal,said(tree_field::minor),number(said_tag::alternatives)),
        binary(op::equal,said(tree_field::minor),number(said_tag::other_alternatives)));
    const auto is_generic = [&](Value major) { return binary(op::equal,major,number(said_tag::generic)); };
    const auto is_optional = binary(op::equal,said(tree_field::major),number(said_tag::optional));
    const auto call = [&](ScriptBuilder::Label label, Value parse_node, Value said_node, std::optional<Value> mode = std::nullopt,
                          std::optional<Value> single_node = std::nullopt) {
        std::vector<Value> args{parameter(1),std::move(parse_node),std::move(said_node)};
        if (mode) args.push_back(*mode);
        if (single_node) args.push_back(*single_node);
        return procedure(label,std::move(args));
    };
    const auto optional_return = [&](unsigned result) {
        const auto done = builder.label();
        branch_if(builder,is_optional,done,false);
        branch_if(builder,binary(op::equal,temporary(result),number(tree_absent)),done,false);
        set_temporary(builder,result,number(tree_matched)); builder.bind(done); return_value(builder,temporary(result));
    };
    {
        constexpr unsigned result = 0;
        const auto incompatible = builder.label(), terminals = builder.label(), parse_terminal = builder.label();
        const auto children = builder.label(), both_children = builder.label(), done = builder.label(), missing = builder.label(), yes = builder.label();
        builder.bind(match); builder.emit(op::allocate_temporaries,{1}); branch_if(builder,parameter(2),missing,false);
        branch_if(builder,is_generic(parsed(tree_field::major)),terminals);
        branch_if(builder,is_generic(said(tree_field::major)),terminals); branch_if(builder,is_optional,terminals);
        branch_if(builder,binary(op::equal,parsed(tree_field::major),said(tree_field::major)),incompatible,false);
        builder.bind(terminals); branch_if(builder,said(tree_field::terminal),parse_terminal,false);
        branch_if(builder,parsed(tree_field::terminal),children,false);
        branch_if(builder,binary(op::equal,said(tree_field::word),number(said_word::none)),incompatible);
        branch_if(builder,binary(op::equal,said(tree_field::word),number(said_word::any)),yes);
        branch_if(builder,binary(op::equal,parsed(tree_field::word),number(said_word::any)),yes);
        branch_if(builder,binary(op::equal,parsed(tree_field::word),said(tree_field::word)),incompatible,false);
        builder.bind(yes); set_temporary(builder,result,number(tree_matched)); builder.branch(op::jump,done);
        builder.bind(children); set_temporary(builder,result,call(scan_parse,parsed(tree_field::child),parameter(3),number(0))); builder.branch(op::jump,done);
        builder.bind(parse_terminal); branch_if(builder,parsed(tree_field::terminal),both_children,false);
        set_temporary(builder,result,call(scan_said,parameter(2),said(tree_field::child),alternatives,number(1))); builder.branch(op::jump,done);
        builder.bind(both_children);
        set_temporary(builder,result,call(scan_said,parsed(tree_field::child),said(tree_field::child),alternatives,number(0)));
        builder.branch(op::jump,done);
        builder.bind(incompatible); set_temporary(builder,result,number(tree_mismatched)); builder.branch(op::jump,done);
        builder.bind(missing); set_temporary(builder,result,number(tree_absent));
        builder.bind(done); optional_return(result);
    }
    {
        constexpr unsigned cursor = 0, result = 1;
        const auto scan = builder.label(), advance = builder.label(), done = builder.label();
        builder.bind(scan_said); builder.emit(op::allocate_temporaries,{2});
        set_temporary(builder,cursor,parameter(3)); set_temporary(builder,result,number(tree_matched));
        builder.bind(scan); branch_if(builder,temporary(cursor),done,false);
        branch_if(builder,binary(op::equal,indexed_local(tree_field::major,temporary(cursor)),number(said_tag::ignored)),advance);
        set_temporary(builder,result,call(scan_parse,parameter(2),temporary(cursor),parameter(5)));
        const auto conjunction = builder.label(); branch_if(builder,parameter(4),conjunction,false);
        branch_if(builder,binary(op::equal,temporary(result),number(tree_matched)),done); builder.branch(op::jump,advance);
        builder.bind(conjunction); branch_if(builder,binary(op::equal,temporary(result),number(tree_matched)),done,false);
        builder.bind(advance); set_temporary(builder,cursor,indexed_local(tree_field::sibling,temporary(cursor))); builder.branch(op::jump,scan);
        builder.bind(done); return_value(builder,temporary(result));
    }
    {
        constexpr unsigned cursor = 0, result = 1, trial = 2;
        const auto scan = builder.label(), advance = builder.label(), done = builder.label(), marker = builder.label();
        const auto unwrap = builder.label(), ordinary = builder.label(), single = builder.label();
        builder.bind(scan_parse); builder.emit(op::allocate_temporaries,{3});
        branch_if(builder,binary(op::equal,said(tree_field::major),number(said_tag::partial)),marker);
        set_temporary(builder,result,number(tree_absent));
        branch_if(builder,said(tree_field::terminal),ordinary);
        branch_if(builder,is_generic(said(tree_field::major)),unwrap); branch_if(builder,is_optional,unwrap);
        builder.bind(ordinary); branch_if(builder,parameter(4),single); set_temporary(builder,cursor,parameter(2));
        builder.bind(scan); branch_if(builder,temporary(cursor),done,false);
        const auto parse_major = candidate_word(parameter(1),binary(op::add,temporary(cursor),number(tree_field::major)));
        const auto eligible = builder.label(); branch_if(builder,is_generic(parse_major),eligible);
        branch_if(builder,binary(op::equal,parse_major,said(tree_field::major)),advance,false);
        builder.bind(eligible); set_temporary(builder,trial,call(match,temporary(cursor),parameter(3)));
        branch_if(builder,temporary(trial),advance,false); set_temporary(builder,result,temporary(trial));
        branch_if(builder,binary(op::equal,temporary(result),number(tree_matched)),done);
        builder.bind(advance); set_temporary(builder,cursor,candidate_word(parameter(1),binary(op::add,temporary(cursor),number(tree_field::sibling))));
        builder.branch(op::jump,scan);
        builder.bind(single); set_temporary(builder,result,call(match,parameter(2),parameter(3))); builder.branch(op::jump,done);
        builder.bind(unwrap); set_temporary(builder,result,call(scan_said,parameter(2),said(tree_field::child),alternatives,parameter(4))); builder.branch(op::jump,done);
        builder.bind(marker); set_local(builder,partial,number(1)); return_value(builder,number(tree_matched));
        builder.bind(done); optional_return(result);
    }
    builder.bind(entry); set_local(builder,partial,number(0)); return_value(builder,call(match,parameter(2),parameter(3)));
    return {entry,partial};
}
} // namespace scimate::parser_detail
