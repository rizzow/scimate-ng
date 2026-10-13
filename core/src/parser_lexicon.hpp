// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_bytecode.hpp"
#include "scimate/assets.hpp"

namespace scimate::parser_detail {
struct LexiconProcedures {
    ScriptBuilder::Label lookup, word_class, group;
    unsigned table, count;
};
// Each source SCI0 spelling has one active meaning: later dictionary entries
// replace earlier entries. Sorted entries permit binary lookup in SCI bytecode.
// Return values are one-based record indices, with zero reserved for no match.
inline LexiconProcedures emit_lexicon(ScriptBuilder& builder, const std::vector<Word>& source) {
    namespace op = sci_vm::opcode;
    constexpr unsigned record_words = 3, spelling_field = 0, class_field = 1, group_field = 2;
    constexpr std::size_t max_records = (sci_vm::max_script_address-sci_vm::section_header_bytes)/(record_words*sci_vm::word_bytes);
    std::map<std::string,Word> words;
    for (const auto& word : source) {
        if (word.text.empty() || word.text.size() > 255 || word.word_class > 0xfff || word.group > 0xfff)
            throw std::runtime_error("portable parser: invalid SCI0 vocabulary entry");
        words[word.text] = word;
    }
    if (words.size() > max_records) throw std::runtime_error("portable parser: vocabulary exceeds script word-index range");
    unsigned table = 0;
    bool first = true;
    for (const auto& entry : words) {
        const auto pointer = builder.string_local(entry.first);
        if (first) { table = pointer; first = false; }
        builder.local(entry.second.word_class); builder.local(entry.second.group);
    }
    const auto count = static_cast<unsigned>(words.size());
    const auto lookup = builder.label(), word_class = builder.label(), group = builder.label();
    const auto search = builder.label(), missing = builder.label(), found = builder.label(), lower = builder.label();
    constexpr unsigned low = 0, high = 1, middle = 2, comparison = 3, temporaries = 4;
    constexpr unsigned word_parameter = 1;
    builder.bind(lookup);
    builder.emit(op::allocate_temporaries,{temporaries});
    builder.emit(op::load_immediate,{0}); builder.emit(op::store_accumulator_to_temporary,{low});
    builder.emit(op::load_immediate,{count}); builder.emit(op::store_accumulator_to_temporary,{high});
    builder.bind(search);
    builder.emit(op::load_temporary_to_stack,{low}); builder.emit(op::load_temporary_to_accumulator,{high});
    builder.emit(op::less_unsigned); builder.branch(op::branch_false,missing);
    builder.emit(op::load_temporary_to_stack,{low}); builder.emit(op::load_temporary_to_accumulator,{high});
    builder.emit(op::add); builder.emit(op::push_accumulator); builder.emit(op::load_immediate,{2}); builder.emit(op::divide);
    builder.emit(op::store_accumulator_to_temporary,{middle});
    builder.emit(op::push_two); builder.emit(op::load_parameter_to_stack,{word_parameter});
    builder.emit(op::load_temporary_to_stack,{middle}); builder.emit(op::load_immediate,{record_words}); builder.emit(op::multiply);
    builder.emit(op::load_indexed_local_to_stack,{table+spelling_field});
    builder.emit(op::call_kernel,{sci_vm::sci1_kernel::string_compare,2*sci_vm::word_bytes});
    builder.emit(op::store_accumulator_to_temporary,{comparison}); builder.branch(op::branch_false,found);
    builder.emit(op::load_temporary_to_stack,{comparison}); builder.emit(op::load_immediate,{0}); builder.emit(op::less_signed);
    builder.branch(op::branch_true,lower);
    builder.emit(op::load_temporary_to_stack,{middle}); builder.emit(op::load_immediate,{1}); builder.emit(op::add);
    builder.emit(op::store_accumulator_to_temporary,{low}); builder.branch(op::jump,search);
    builder.bind(lower);
    builder.emit(op::load_temporary_to_accumulator,{middle}); builder.emit(op::store_accumulator_to_temporary,{high});
    builder.branch(op::jump,search);
    builder.bind(found);
    builder.emit(op::load_temporary_to_stack,{middle}); builder.emit(op::load_immediate,{1}); builder.emit(op::add);
    builder.emit(op::return_);
    builder.bind(missing); builder.emit(op::load_immediate,{0}); builder.emit(op::return_);
    for (const auto& property : {std::pair{word_class,class_field},std::pair{group,group_field}}) {
        const auto invalid_index = builder.label();
        builder.bind(property.first);
        builder.emit(op::load_parameter_to_accumulator,{word_parameter}); builder.branch(op::branch_false,invalid_index);
        builder.emit(op::load_parameter_to_stack,{word_parameter}); builder.emit(op::load_immediate,{count+1}); builder.emit(op::less_unsigned);
        builder.branch(op::branch_false,invalid_index);
        builder.emit(op::load_parameter_to_stack,{word_parameter}); builder.emit(op::load_immediate,{1}); builder.emit(op::subtract);
        builder.emit(op::push_accumulator); builder.emit(op::load_immediate,{record_words}); builder.emit(op::multiply);
        builder.emit(op::load_indexed_local_to_accumulator,{table+property.second}); builder.emit(op::return_);
        builder.bind(invalid_index); builder.emit(op::load_immediate,{0}); builder.emit(op::return_);
    }
    return {lookup,word_class,group,table,count};
}
} // namespace scimate::parser_detail
