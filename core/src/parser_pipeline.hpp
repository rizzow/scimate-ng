// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_sentence.hpp"
#include "parser_tree_builder.hpp"
#include "parser_pronouns.hpp"
#include "parser_synonyms.hpp"

namespace scimate::parser_detail {
struct ParserPipeline {
    ScriptBuilder::Label procedure;
    TokenScanner tokens;
    SentenceParser sentence;
    TreeBuilder tree;
    PronounState pronouns;
    SynonymState synonyms;
};
// Entry (input string) returns the parsed tree arena or zero. Game-specific
// failure callbacks and event claiming belong to the generic kernel bridge,
// which can distinguish lexical, syntax, capacity and tree-format failure state.
inline ParserPipeline emit_parser_pipeline(ScriptBuilder& builder, const std::vector<Word>& words,
    const GrammarTable& grammar, const std::vector<SuffixRule>& suffixes) {
    using namespace codegen;
    const auto normalized = compile_grammar(grammar);
    const auto grammar_layout = emit_compiled_grammar(builder,normalized);
    const auto tables = emit_parser_tables(builder,grammar,suffixes);
    const auto lexicon = emit_lexicon(builder,words);
    const auto resolver = emit_word_resolver(builder,lexicon,tables);
    const auto tokens = emit_token_scanner(builder,resolver);
    const auto rules = emit_rule_selector(builder,grammar_layout);
    const auto candidates = emit_candidate_procedures(builder,grammar_layout);
    const auto sentence = emit_sentence_parser(builder,tokens,grammar_layout,rules,candidates);
    const auto tree = emit_tree_builder(builder,grammar.root_id());
    const auto pronouns = emit_pronoun_state(builder,tokens);
    const auto synonyms = emit_synonym_state(builder,tokens);
    const auto entry = builder.label(), failed = builder.label();
    builder.bind(entry);
    for (const auto pointer : {sentence.result,tree.arena}) {
        const auto empty = builder.label(); branch_if(builder,local(pointer),empty,false);
        kernel(sci_vm::sci1_kernel::memory,{number(sci_vm::memory_operation::free),local(pointer)})(builder);
        builder.bind(empty); set_local(builder,pointer,number(0));
    }
    set_local(builder,sentence.failure,number(0)); set_local(builder,tree.failed,number(0));
    branch_if(builder,procedure(tokens.procedure,{parameter(1)}),failed,false);
    procedure(synonyms.apply,{})(builder);
    procedure(pronouns.replace,{})(builder);
    branch_if(builder,procedure(sentence.procedure,{}),failed,false);
    procedure(tree.procedure,{local(sentence.result)})(builder);
    kernel(sci_vm::sci1_kernel::memory,{number(sci_vm::memory_operation::free),local(sentence.result)})(builder);
    set_local(builder,sentence.result,number(0));
    branch_if(builder,local(tree.arena),failed,false); procedure(pronouns.remember,{local(tree.arena)})(builder);
    return_value(builder,local(tree.arena));
    builder.bind(failed); return_value(builder,number(0));
    return {entry,tokens,sentence,tree,pronouns,synonyms};
}
} // namespace scimate::parser_detail
