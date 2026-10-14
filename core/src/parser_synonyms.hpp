// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_scanner.hpp"
#include "parser_candidates.hpp"

namespace scimate::parser_detail {
struct SynonymPair { unsigned original, replacement; };
inline std::vector<SynonymPair> read_synonym_pairs(const Bytes& bytes) {
    constexpr unsigned pair_bytes = 2*sci_vm::word_bytes;
    if (bytes.size()%pair_bytes) invalid_parser_table("incomplete synonym pair");
    std::vector<SynonymPair> pairs;
    for (std::size_t offset = 0; offset < bytes.size(); offset += pair_bytes)
        pairs.push_back({table_word(bytes,offset),table_word(bytes,offset+sci_vm::word_bytes)});
    return pairs;
}
struct SynonymState { unsigned blocks; ScriptBuilder::Label clear, append, apply; };
// Active blocks retain their order and own their memory. append(pointer,count)
// copies raw little-endian word pairs; applying them permits chained aliases.
// Source-script loading/list selection is supplied by the eventual bridge.
inline SynonymState emit_synonym_state(ScriptBuilder& builder, const TokenScanner& tokens) {
    namespace op = sci_vm::opcode;
    namespace kernels = sci_vm::sci1_kernel;
    using namespace codegen;
    constexpr unsigned header_words = 1, pair_words = 2;
    constexpr unsigned max_block_pairs = (sci_vm::max_script_address-sci_vm::word_bytes)/(pair_words*sci_vm::word_bytes);
    const auto blocks = builder.local();
    const auto clear = builder.label(), append = builder.label(), apply = builder.label();
    {
        constexpr unsigned pointer = 0;
        const auto loop = builder.label(), done = builder.label();
        builder.bind(clear); builder.emit(op::allocate_temporaries,{1}); branch_if(builder,local(blocks),done,false);
        const auto dispose = builder.label();
        builder.bind(loop); branch_if(builder,kernel(kernels::first_node,{local(blocks)}),dispose,false);
        set_temporary(builder,pointer,kernel(kernels::node_value,{kernel(kernels::first_node,{local(blocks)})}));
        kernel(kernels::delete_key,{local(blocks),temporary(pointer)})(builder);
        kernel(kernels::memory,{number(sci_vm::memory_operation::free),temporary(pointer)})(builder); builder.branch(op::jump,loop);
        builder.bind(dispose); kernel(kernels::dispose_list,{local(blocks)})(builder);
        builder.bind(done); set_local(builder,blocks,number(0)); return_value(builder,number(1));
    }
    {
        constexpr unsigned pointer = 0;
        const auto empty = builder.label(), failure = builder.label(), ready = builder.label();
        builder.bind(append); builder.emit(op::allocate_temporaries,{1}); branch_if(builder,parameter(2),empty,false);
        branch_if(builder,binary(op::less_unsigned,parameter(2),number(max_block_pairs+1)),failure,false);
        const auto data_bytes = binary(op::multiply,parameter(2),number(pair_words*sci_vm::word_bytes));
        set_temporary(builder,pointer,kernel(kernels::memory,{number(sci_vm::memory_operation::allocate),
            binary(op::add,data_bytes,number(header_words*sci_vm::word_bytes))}));
        branch_if(builder,temporary(pointer),failure,false);
        set_candidate_word(builder,temporary(pointer),number(0),parameter(2));
        kernel(kernels::memory,{number(sci_vm::memory_operation::copy),
            binary(op::add,temporary(pointer),number(header_words*sci_vm::word_bytes)),parameter(1),data_bytes})(builder);
        branch_if(builder,local(blocks),ready); set_local(builder,blocks,kernel(kernels::new_list,{}));
        builder.bind(ready); kernel(kernels::add_to_end,{local(blocks),kernel(kernels::new_node,{temporary(pointer),temporary(pointer)})})(builder);
        builder.bind(empty); return_value(builder,number(1)); builder.bind(failure); return_value(builder,number(0));
    }
    {
        constexpr unsigned token = 0, node = 1, block = 2, pair = 3, count = 4, group = 5;
        const auto token_loop = builder.label(), block_loop = builder.label(), pair_loop = builder.label(), next_pair = builder.label();
        const auto next_block = builder.label(), next_token = builder.label(), done = builder.label();
        builder.bind(apply); builder.emit(op::allocate_temporaries,{6}); branch_if(builder,local(blocks),done,false);
        set_temporary(builder,token,number(0)); builder.bind(token_loop);
        branch_if(builder,binary(op::less_unsigned,temporary(token),local(tokens.count)),done,false);
        set_temporary(builder,group,indexed_local(tokens.groups,temporary(token)));
        set_temporary(builder,node,kernel(kernels::first_node,{local(blocks)}));
        builder.bind(block_loop); branch_if(builder,temporary(node),next_token,false);
        set_temporary(builder,block,kernel(kernels::node_value,{temporary(node)}));
        set_temporary(builder,count,candidate_word(temporary(block),number(0))); set_temporary(builder,pair,number(0));
        builder.bind(pair_loop); branch_if(builder,binary(op::less_unsigned,temporary(pair),temporary(count)),next_block,false);
        const auto offset = binary(op::add,number(header_words),binary(op::multiply,temporary(pair),number(pair_words)));
        branch_if(builder,binary(op::equal,temporary(group),candidate_word(temporary(block),offset)),next_pair,false);
        set_temporary(builder,group,candidate_word(temporary(block),binary(op::add,offset,number(1))));
        builder.bind(next_pair); builder.emit(op::increment_temporary_to_accumulator,{pair}); builder.branch(op::jump,pair_loop);
        builder.bind(next_block); set_temporary(builder,node,kernel(kernels::next_node,{temporary(node)})); builder.branch(op::jump,block_loop);
        builder.bind(next_token); set_indexed_local(builder,tokens.groups,temporary(token),temporary(group));
        builder.emit(op::increment_temporary_to_accumulator,{token}); builder.branch(op::jump,token_loop);
        builder.bind(done); return_value(builder,number(1));
    }
    return {blocks,clear,append,apply};
}
} // namespace scimate::parser_detail
