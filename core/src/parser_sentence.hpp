// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_candidates.hpp"
#include "parser_scanner.hpp"

namespace scimate::parser_detail {
enum class SentenceFailure : unsigned { none, syntax, capacity };
struct SentenceParser { ScriptBuilder::Label procedure; unsigned result, failure; };
inline SentenceParser emit_sentence_parser(ScriptBuilder& builder, const TokenScanner& tokens,
    const CompiledGrammarLayout& grammar, ScriptBuilder::Label select_rule, const CandidateProcedures& candidates) {
    namespace op = sci_vm::opcode;
    namespace kernels = sci_vm::sci1_kernel;
    using namespace codegen;
    constexpr unsigned max_candidates = 512;
    const auto result = builder.local(), failure = builder.local();
    const auto free_candidate = [](Value pointer) { return kernel(kernels::memory,{number(sci_vm::memory_operation::free),pointer}); };
    const auto enqueue = [&](Value list, Value pointer) {
        kernel(kernels::add_to_end,{list,kernel(kernels::new_node,{pointer,pointer})})(builder);
    };
    const auto take = [&](unsigned list_temp, unsigned pointer_temp) {
        set_temporary(builder,pointer_temp,kernel(kernels::node_value,{kernel(kernels::first_node,{temporary(list_temp)})}));
        kernel(kernels::delete_key,{temporary(list_temp),temporary(pointer_temp)})(builder);
    };
    const auto cleanup = builder.label();
    {
        const auto loop = builder.label(), done = builder.label();
        builder.bind(cleanup); builder.emit(op::allocate_temporaries,{1});
        builder.bind(loop); branch_if(builder,kernel(kernels::first_node,{parameter(1)}),done,false);
        set_temporary(builder,0,kernel(kernels::node_value,{kernel(kernels::first_node,{parameter(1)})}));
        kernel(kernels::delete_key,{parameter(1),temporary(0)})(builder); free_candidate(temporary(0))(builder);
        builder.branch(op::jump,loop); builder.bind(done); return_value(builder,number(0));
    }
    constexpr unsigned work = 0, next_work = 1, candidate = 2, word = 3, rule = 4;
    constexpr unsigned nonterminal = 5, expanded = 6, count = 7, temporaries = 8;
    const auto entry = builder.label(), no_previous = builder.label(), seed = builder.label(), seeded = builder.label();
    const auto word_loop = builder.label(), candidate_loop = builder.label(), after_word = builder.label(), discard = builder.label();
    const auto final_word = builder.label(), expand = builder.label(), capacity = builder.label(), syntax = builder.label();
    const auto failed = builder.label(), finished = builder.label(), keep = builder.label();
    const auto current_class = indexed_local(tokens.classes,temporary(word));
    const auto current_group = indexed_local(tokens.groups,temporary(word));
    builder.bind(entry); builder.emit(op::allocate_temporaries,{temporaries});
    branch_if(builder,local(result),no_previous,false); free_candidate(local(result))(builder);
    builder.bind(no_previous); set_local(builder,result,number(0)); set_local(builder,failure,number(0));
    set_temporary(builder,candidate,number(0)); set_temporary(builder,expanded,number(0));
    set_temporary(builder,work,kernel(kernels::new_list,{})); set_temporary(builder,next_work,kernel(kernels::new_list,{}));
    branch_if(builder,local(tokens.count),syntax,false);
    set_temporary(builder,word,number(0)); set_temporary(builder,rule,number(0)); set_temporary(builder,count,number(0));
    builder.bind(seed);
    set_temporary(builder,rule,procedure(select_rule,{number(grammar.start_id),current_class,current_group,temporary(rule)}));
    branch_if(builder,temporary(rule),seeded,false);
    branch_if(builder,binary(op::less_unsigned,temporary(count),number(max_candidates)),capacity,false);
    set_temporary(builder,expanded,procedure(candidates.splice_rule,{number(0),temporary(rule)}));
    branch_if(builder,temporary(expanded),capacity,false); enqueue(temporary(work),temporary(expanded));
    set_temporary(builder,expanded,number(0)); builder.emit(op::increment_temporary_to_accumulator,{count}); builder.branch(op::jump,seed);
    builder.bind(seeded);
    builder.bind(word_loop); set_temporary(builder,count,number(0));
    builder.bind(candidate_loop);
    branch_if(builder,kernel(kernels::first_node,{temporary(work)}),after_word,false);
    take(work,candidate);
    branch_if(builder,procedure(candidates.consume_word,{temporary(candidate),current_class,current_group}),discard,false);
    branch_if(builder,binary(op::equal,binary(op::add,temporary(word),number(1)),local(tokens.count)),final_word);
    set_temporary(builder,nonterminal,procedure(candidates.find_pending,{temporary(candidate),number(1)}));
    branch_if(builder,temporary(nonterminal),discard,false);
    const auto symbol_value = binary(op::add,number(candidate_header_words+1),
        binary(op::multiply,binary(op::subtract,temporary(nonterminal),number(1)),number(candidate_symbol_words)));
    set_temporary(builder,nonterminal,candidate_word(temporary(candidate),symbol_value)); set_temporary(builder,rule,number(0));
    builder.bind(expand);
    const auto next_word = binary(op::add,temporary(word),number(1));
    set_temporary(builder,rule,procedure(select_rule,{temporary(nonterminal),indexed_local(tokens.classes,next_word),
        indexed_local(tokens.groups,next_word),temporary(rule)}));
    branch_if(builder,temporary(rule),discard,false);
    branch_if(builder,binary(op::less_unsigned,temporary(count),number(max_candidates)),capacity,false);
    set_temporary(builder,expanded,procedure(candidates.splice_rule,{temporary(candidate),temporary(rule)}));
    branch_if(builder,temporary(expanded),capacity,false); enqueue(temporary(next_work),temporary(expanded));
    set_temporary(builder,expanded,number(0)); builder.emit(op::increment_temporary_to_accumulator,{count}); builder.branch(op::jump,expand);
    builder.bind(final_word); branch_if(builder,procedure(candidates.find_pending,{temporary(candidate),number(0)}),discard);
    branch_if(builder,local(result),discard); set_local(builder,result,temporary(candidate)); builder.branch(op::jump,keep);
    builder.bind(discard); free_candidate(temporary(candidate))(builder);
    builder.bind(keep); set_temporary(builder,candidate,number(0)); builder.branch(op::jump,candidate_loop);
    builder.bind(after_word); builder.emit(op::increment_temporary_to_accumulator,{word});
    branch_if(builder,binary(op::equal,temporary(word),local(tokens.count)),finished);
    branch_if(builder,kernel(kernels::first_node,{temporary(next_work)}),syntax,false);
    set_temporary(builder,work,temporary(next_work)); set_temporary(builder,next_work,kernel(kernels::new_list,{}));
    builder.branch(op::jump,word_loop);
    builder.bind(finished); branch_if(builder,local(result),syntax,false); return_value(builder,number(1));
    builder.bind(capacity); set_local(builder,failure,number(static_cast<unsigned>(SentenceFailure::capacity))); builder.branch(op::jump,failed);
    builder.bind(syntax); set_local(builder,failure,number(static_cast<unsigned>(SentenceFailure::syntax)));
    builder.bind(failed);
    const auto no_candidate = builder.label(), no_expanded = builder.label();
    branch_if(builder,temporary(candidate),no_candidate,false); free_candidate(temporary(candidate))(builder);
    builder.bind(no_candidate); branch_if(builder,temporary(expanded),no_expanded,false); free_candidate(temporary(expanded))(builder);
    builder.bind(no_expanded); procedure(cleanup,{temporary(work)})(builder); procedure(cleanup,{temporary(next_work)})(builder);
    return_value(builder,number(0));
    return {entry,result,failure};
}
} // namespace scimate::parser_detail
