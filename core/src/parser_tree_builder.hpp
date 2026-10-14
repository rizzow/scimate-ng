// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_tree.hpp"
#include "parser_said.hpp"

namespace scimate::parser_detail {
struct TreeBuilder { ScriptBuilder::Label procedure; unsigned arena, failed; };
// Converts a completed candidate's open/tag/word/close stream to the shared tree
// record format. The previous arena is released at entry; malformed streams and
// capacity failures release the new arena rather than returning a partial tree.
inline TreeBuilder emit_tree_builder(ScriptBuilder& builder, unsigned root_minor) {
    namespace op = sci_vm::opcode;
    using namespace codegen;
    using Kind = GrammarSymbolKind;
    const auto arena = builder.local(), cursor = builder.local(), failed = builder.local();
    const auto allocate_node = builder.label(), children = builder.label(), entry = builder.label();
    const auto field = [](Value pointer, Value node, unsigned member) {
        return candidate_word(pointer,binary(op::add,node,number(member)));
    };
    const auto set_field = [&](Value pointer, Value node, unsigned member, Value value) {
        set_candidate_word(builder,pointer,binary(op::add,node,number(member)),value);
    };
    {
        constexpr unsigned count = 0, node = 1;
        const auto full = builder.label();
        builder.bind(allocate_node); builder.emit(op::allocate_temporaries,{2});
        set_temporary(builder,count,candidate_word(parameter(1),number(0)));
        branch_if(builder,binary(op::less_unsigned,temporary(count),number(tree_node_limit)),full,false);
        set_temporary(builder,node,binary(op::add,number(tree_header_words),binary(op::multiply,temporary(count),number(tree_field::words))));
        set_candidate_word(builder,parameter(1),number(0),binary(op::add,temporary(count),number(1)));
        for (unsigned member = 0; member < tree_field::words; ++member) set_field(parameter(1),temporary(node),member,number(0));
        return_value(builder,temporary(node)); builder.bind(full); return_value(builder,number(0));
    }
    {
        // (candidate pointer, arena pointer, parent word-index, expect close).
        constexpr unsigned previous = 0, kind = 1, node = 2, major = 3, minor = 4;
        const auto scan = builder.label(), close = builder.label(), word = builder.label(), open = builder.label();
        const auto done = builder.label(), fail = builder.label(), first = builder.label(), attached = builder.label();
        const auto candidate = parameter(1), output = parameter(2), parent = parameter(3);
        const auto stream_field = [&](unsigned ahead, unsigned member) {
            return candidate_word(candidate,binary(op::add,number(candidate_header_words+member),
                binary(op::multiply,binary(op::add,local(cursor),number(ahead)),number(candidate_symbol_words))));
        };
        builder.bind(children); builder.emit(op::allocate_temporaries,{5}); set_temporary(builder,previous,number(0));
        builder.bind(scan);
        branch_if(builder,binary(op::less_unsigned,local(cursor),candidate_word(candidate,number(0))),done,false);
        set_temporary(builder,kind,stream_field(0,0));
        branch_if(builder,binary(op::equal,temporary(kind),number(static_cast<unsigned>(Kind::close))),close);
        branch_if(builder,binary(op::equal,temporary(kind),number(static_cast<unsigned>(Kind::word))),word);
        branch_if(builder,binary(op::equal,temporary(kind),number(static_cast<unsigned>(Kind::open))),open);
        builder.branch(op::jump,fail);
        builder.bind(word);
        branch_if(builder,field(output,parent,tree_field::terminal),fail);
        branch_if(builder,field(output,parent,tree_field::child),fail);
        set_field(output,parent,tree_field::terminal,number(1)); set_field(output,parent,tree_field::word,stream_field(0,1));
        set_local(builder,cursor,binary(op::add,local(cursor),number(1))); builder.branch(op::jump,scan);
        builder.bind(open); branch_if(builder,field(output,parent,tree_field::terminal),fail);
        branch_if(builder,binary(op::less_unsigned,binary(op::add,local(cursor),number(2)),candidate_word(candidate,number(0))),fail,false);
        for (unsigned ahead = 1; ahead <= 2; ++ahead)
            branch_if(builder,binary(op::equal,stream_field(ahead,0),number(static_cast<unsigned>(Kind::tag))),fail,false);
        set_temporary(builder,major,stream_field(1,1)); set_temporary(builder,minor,stream_field(2,1));
        set_temporary(builder,node,procedure(allocate_node,{output})); branch_if(builder,temporary(node),fail,false);
        set_field(output,temporary(node),tree_field::major,temporary(major)); set_field(output,temporary(node),tree_field::minor,temporary(minor));
        branch_if(builder,temporary(previous),first,false);
        set_field(output,temporary(previous),tree_field::sibling,temporary(node)); builder.branch(op::jump,attached);
        builder.bind(first); set_field(output,parent,tree_field::child,temporary(node));
        builder.bind(attached); set_temporary(builder,previous,temporary(node));
        set_local(builder,cursor,binary(op::add,local(cursor),number(3)));
        branch_if(builder,procedure(children,{candidate,output,temporary(node),number(1)}),fail,false);
        builder.branch(op::jump,scan);
        builder.bind(close); branch_if(builder,parameter(4),fail,false);
        set_local(builder,cursor,binary(op::add,local(cursor),number(1))); return_value(builder,number(1));
        builder.bind(done); branch_if(builder,parameter(4),fail); return_value(builder,number(1));
        builder.bind(fail); set_local(builder,failed,number(1)); return_value(builder,number(0));
    }
    {
        const auto no_previous = builder.label(), failure = builder.label(), no_allocation = builder.label();
        const auto free = [&](Value pointer) { kernel(sci_vm::sci1_kernel::memory,{number(sci_vm::memory_operation::free),pointer})(builder); };
        builder.bind(entry);
        branch_if(builder,local(arena),no_previous,false); free(local(arena));
        builder.bind(no_previous); set_local(builder,arena,number(0)); set_local(builder,cursor,number(0)); set_local(builder,failed,number(0));
        branch_if(builder,parameter(1),failure,false);
        constexpr unsigned bytes = (tree_header_words+tree_node_limit*tree_field::words)*sci_vm::word_bytes;
        set_local(builder,arena,kernel(sci_vm::sci1_kernel::memory,{number(sci_vm::memory_operation::allocate),number(bytes)}));
        branch_if(builder,local(arena),failure,false); set_candidate_word(builder,local(arena),number(0),number(0));
        procedure(allocate_node,{local(arena)})(builder);
        set_field(local(arena),number(tree_root_index),tree_field::major,number(said_tag::generic));
        set_field(local(arena),number(tree_root_index),tree_field::minor,number(root_minor));
        branch_if(builder,procedure(children,{parameter(1),local(arena),number(tree_root_index),number(0)}),failure,false);
        return_value(builder,local(arena));
        builder.bind(failure); set_local(builder,failed,number(1)); branch_if(builder,local(arena),no_allocation,false);
        free(local(arena)); set_local(builder,arena,number(0)); builder.bind(no_allocation); return_value(builder,number(0));
    }
    return {entry,arena,failed};
}
} // namespace scimate::parser_detail
