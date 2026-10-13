// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_tables.hpp"
#include <algorithm>
#include <tuple>

namespace scimate::parser_detail {
// The replacement runtime uses word-sized kind/value pairs rather than native
// parser pointers. Open/close and tag symbols retain semantic tree structure
// while nonterminals are expanded ahead of time by the migration exporter.
enum class GrammarSymbolKind : unsigned {
    nonterminal, class_mask, group_mask, open, close, tag, word
};
struct GrammarSymbol {
    GrammarSymbolKind kind;
    unsigned value = 0;
    bool operator<(const GrammarSymbol& other) const { return std::tie(kind,value) < std::tie(other.kind,other.value); }
    bool operator==(const GrammarSymbol& other) const { return kind == other.kind && value == other.value; }
    bool demands_input() const {
        return kind == GrammarSymbolKind::nonterminal || kind == GrammarSymbolKind::class_mask || kind == GrammarSymbolKind::group_mask;
    }
};
struct ParserProduction {
    unsigned id;
    std::vector<GrammarSymbol> symbols;
    std::size_t first_demand() const {
        const auto found = std::find_if(symbols.begin(),symbols.end(),[](const auto& symbol) { return symbol.demands_input(); });
        if (found == symbols.end()) invalid_parser_table("production has no input symbol");
        return static_cast<std::size_t>(found-symbols.begin());
    }
};
struct CompiledGrammar {
    unsigned root_id, start_id;
    std::vector<ParserProduction> productions;
};
struct GrammarLimits {
    std::size_t productions = 4096, symbols = 262144;
};
inline ParserProduction lower_production(const GrammarRecord& record) {
    using Kind = GrammarSymbolKind;
    ParserProduction production{record[0],{}};
    for (std::size_t field = 1; field+1 < record.size() && record[field]; field += 2) {
        const auto tag = record[field], value = record[field+1];
        if (tag == grammar_tag::class_mask) production.symbols.push_back({Kind::class_mask,value});
        else if (tag == grammar_tag::group_mask) production.symbols.push_back({Kind::group_mask,value});
        else if (tag == grammar_tag::stored_word)
            // Native engines disagree about this debugging opcode's input
            // consumption. Reject it until its runtime semantics are proven.
            invalid_parser_table("force-storage grammar rules require a semantic adaptation");
        else {
            production.symbols.push_back({Kind::open});
            production.symbols.push_back({Kind::tag,tag});
            production.symbols.push_back({Kind::tag,value});
            production.symbols.push_back({Kind::nonterminal,value});
            production.symbols.push_back({Kind::close});
        }
    }
    production.first_demand();
    return production;
}
// Normalize the first consuming symbol to a terminal. A frontier prevents
// revisiting completed derivations; source order determines alternative order.
// Cyclic expansion is bounded and fails explicitly, never emits a partial table.
inline CompiledGrammar compile_grammar(const GrammarTable& source, GrammarLimits limits = {}) {
    using Kind = GrammarSymbolKind;
    CompiledGrammar compiled{source.root_id(),source.start_id(),{}};
    std::vector<ParserProduction> deferred, frontier;
    std::set<std::pair<unsigned,std::vector<GrammarSymbol>>> seen;
    std::size_t total_symbols = 0;
    const auto accept = [&](const ParserProduction& production, std::vector<ParserProduction>& destination) {
        if (!seen.emplace(production.id,production.symbols).second) return;
        if (seen.size() > limits.productions || production.symbols.size() > limits.symbols-total_symbols)
            invalid_parser_table("grammar normalization exceeds configured limits");
        total_symbols += production.symbols.size();
        destination.push_back(production);
    };
    for (std::size_t i = 1; i < source.records.size(); ++i) {
        auto production = lower_production(source.records[i]);
        const auto& first = production.symbols[production.first_demand()];
        if (first.kind == Kind::nonterminal) deferred.push_back(std::move(production));
        else accept(production,frontier);
    }
    while (!frontier.empty()) {
        std::vector<ParserProduction> next;
        for (const auto& prefix : deferred) {
            const auto index = prefix.first_demand();
            for (const auto& replacement : frontier) {
                if (prefix.symbols[index].value != replacement.id) continue;
                ParserProduction expanded{prefix.id,{}};
                expanded.symbols.reserve(prefix.symbols.size()-1+replacement.symbols.size());
                expanded.symbols.insert(expanded.symbols.end(),prefix.symbols.begin(),prefix.symbols.begin()+index);
                expanded.symbols.insert(expanded.symbols.end(),replacement.symbols.begin(),replacement.symbols.end());
                expanded.symbols.insert(expanded.symbols.end(),prefix.symbols.begin()+index+1,prefix.symbols.end());
                accept(expanded,next);
            }
        }
        compiled.productions.insert(compiled.productions.end(),frontier.begin(),frontier.end());
        frontier = std::move(next);
    }
    const auto start = std::find_if(compiled.productions.begin(),compiled.productions.end(),
        [&](const auto& production) { return production.id == compiled.start_id; });
    if (start == compiled.productions.end()) invalid_parser_table("grammar start rule cannot consume input");
    for (const auto& production : compiled.productions) {
        const auto first = production.first_demand();
        for (std::size_t i = first+1; i < production.symbols.size(); ++i) {
            const auto kind = production.symbols[i].kind;
            if (kind == Kind::class_mask || kind == Kind::group_mask)
                invalid_parser_table("grammar production needs more than one immediate terminal");
        }
    }
    return compiled;
}
struct CompiledGrammarLayout {
    unsigned count_local, offsets_local, root_id, start_id;
};
inline CompiledGrammarLayout emit_compiled_grammar(ScriptBuilder& builder, const CompiledGrammar& grammar) {
    const auto count = builder.local(static_cast<unsigned>(grammar.productions.size()));
    const auto offsets = count+1;
    for (std::size_t i = 0; i < grammar.productions.size(); ++i) builder.local();
    for (std::size_t i = 0; i < grammar.productions.size(); ++i) {
        const auto& production = grammar.productions[i];
        const auto start = builder.local(production.id);
        builder.set_local(offsets+static_cast<unsigned>(i),start);
        builder.local(static_cast<unsigned>(production.symbols.size()));
        builder.local(static_cast<unsigned>(production.first_demand()));
        for (const auto& symbol : production.symbols) {
            builder.local(static_cast<unsigned>(symbol.kind)); builder.local(symbol.value);
        }
    }
    return {count,offsets,grammar.root_id,grammar.start_id};
}
// Runtime entry: (nonterminal, word class, word group, zero-based start index).
// Returns the next matching production's one-based index, or zero. The caller
// can enumerate all alternatives without losing source ordering or ambiguity.
inline ScriptBuilder::Label emit_rule_selector(ScriptBuilder& builder, const CompiledGrammarLayout& layout) {
    namespace op = sci_vm::opcode;
    using Kind = GrammarSymbolKind;
    constexpr unsigned cursor = 0, entry = 1, symbol = 2, temporaries = 3;
    constexpr unsigned nonterminal_parameter = 1, class_parameter = 2, group_parameter = 3, start_parameter = 4;
    constexpr unsigned first_demand_field = 2, symbol_fields = 3, symbol_words = 2, value_field = 1;
    const auto procedure = builder.label(), scan = builder.label(), next = builder.label();
    const auto group = builder.label(), compare = builder.label(), missing = builder.label();
    builder.bind(procedure); builder.emit(op::allocate_temporaries,{temporaries});
    builder.emit(op::load_parameter_to_accumulator,{start_parameter}); builder.emit(op::store_accumulator_to_temporary,{cursor});
    builder.bind(scan);
    builder.emit(op::load_temporary_to_stack,{cursor}); builder.emit(op::load_local_to_accumulator,{layout.count_local});
    builder.emit(op::less_unsigned); builder.branch(op::branch_false,missing);
    builder.emit(op::load_temporary_to_accumulator,{cursor}); builder.emit(op::load_indexed_local_to_accumulator,{layout.offsets_local});
    builder.emit(op::store_accumulator_to_temporary,{entry});
    builder.emit(op::load_indexed_local_to_stack,{0}); builder.emit(op::load_parameter_to_accumulator,{nonterminal_parameter});
    builder.emit(op::equal); builder.branch(op::branch_false,next);
    builder.emit(op::load_temporary_to_accumulator,{entry}); builder.emit(op::load_indexed_local_to_stack,{first_demand_field});
    builder.emit(op::load_immediate,{symbol_words}); builder.emit(op::multiply); builder.emit(op::push_accumulator);
    builder.emit(op::load_temporary_to_accumulator,{entry}); builder.emit(op::add); builder.emit(op::push_accumulator);
    builder.emit(op::load_immediate,{symbol_fields}); builder.emit(op::add); builder.emit(op::store_accumulator_to_temporary,{symbol});
    builder.emit(op::load_indexed_local_to_stack,{0}); builder.emit(op::load_immediate,{static_cast<unsigned>(Kind::class_mask)});
    builder.emit(op::equal); builder.branch(op::branch_false,group);
    builder.emit(op::load_parameter_to_accumulator,{class_parameter}); builder.branch(op::jump,compare);
    builder.bind(group);
    builder.emit(op::load_temporary_to_accumulator,{symbol}); builder.emit(op::load_indexed_local_to_stack,{0});
    builder.emit(op::load_immediate,{static_cast<unsigned>(Kind::group_mask)}); builder.emit(op::equal);
    builder.branch(op::branch_false,next); builder.emit(op::load_parameter_to_accumulator,{group_parameter});
    builder.bind(compare);
    builder.emit(op::push_accumulator); builder.emit(op::load_temporary_to_accumulator,{symbol});
    builder.emit(op::load_indexed_local_to_accumulator,{value_field}); builder.emit(op::bitwise_and);
    builder.branch(op::branch_false,next);
    builder.emit(op::load_temporary_to_stack,{cursor}); builder.emit(op::load_immediate,{1}); builder.emit(op::add); builder.emit(op::return_);
    builder.bind(next); builder.emit(op::increment_temporary_to_accumulator,{cursor}); builder.branch(op::jump,scan);
    builder.bind(missing); builder.emit(op::load_immediate,{0}); builder.emit(op::return_);
    return procedure;
}
} // namespace scimate::parser_detail
