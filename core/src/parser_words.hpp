// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_codegen.hpp"
#include "parser_lexicon.hpp"
#include "parser_tables.hpp"

namespace scimate::parser_detail {
inline constexpr unsigned parser_word_bytes = 256, numeric_word_class = 0x100, numeric_word_group = 0xffd;
inline unsigned allocate_word_buffer(ScriptBuilder& builder) {
    const auto start = builder.local();
    for (unsigned i = 1; i < parser_word_bytes/sci_vm::word_bytes; ++i) builder.local();
    return start;
}
struct WordResolver {
    ScriptBuilder::Label procedure;
    unsigned word_class, group;
};
// Input is a lowercased, null-terminated word. Exact SCI0 meanings take priority,
// then suffix rules in source order, then a complete nonnegative decimal word.
inline WordResolver emit_word_resolver(ScriptBuilder& builder, const LexiconProcedures& lexicon,
                                      const ParserTableLayout& tables) {
    namespace op = sci_vm::opcode;
    namespace kernel = sci_vm::sci1_kernel;
    using namespace codegen;
    const auto word_class = builder.local(), group = builder.local();
    const auto normalized = allocate_word_buffer(builder), reduced = allocate_word_buffer(builder);
    constexpr unsigned raw_index = 0, length = 1, character = 2, record = 3, suffix_index = 4;
    constexpr unsigned suffix_offset = 5, stem_length = 6, alternate_length = 7, temporaries = 8;
    constexpr unsigned input_parameter = 1, suffix_record_words = 4;
    constexpr unsigned alternative_field = 0, replacement_field = 1, result_class_field = 2, mask_field = 3;
    const auto entry = builder.label(), copy = builder.label(), advance = builder.label(), exact = builder.label();
    const auto found = builder.label(), suffix = builder.label(), next_suffix = builder.label();
    const auto numeric = builder.label(), digit = builder.label(), success = builder.label(), failure = builder.label();
    const auto raw = parameter(input_parameter), norm = address(normalized), output = address(reduced);
    const auto ch = temporary(character), len = temporary(length);
    const auto suffix_field = [&](unsigned field) { return indexed_local(tables.suffix_start+field,temporary(suffix_offset)); };
    const auto resolve_record = [&] {
        set_local(builder,word_class,procedure(lexicon.word_class,{temporary(record)}));
        set_local(builder,group,procedure(lexicon.group,{temporary(record)}));
    };
    builder.bind(entry); builder.emit(op::allocate_temporaries,{temporaries});
    set_local(builder,word_class,number(0)); set_local(builder,group,number(0));
    set_temporary(builder,raw_index,number(0)); set_temporary(builder,length,number(0));
    builder.bind(copy);
    branch_if(builder,binary(op::less_unsigned,temporary(raw_index),number(parser_word_bytes)),failure,false);
    set_temporary(builder,character,codegen::kernel(kernel::string_at,{raw,temporary(raw_index)}));
    branch_if(builder,ch,exact,false);
    branch_if(builder,binary(op::equal,ch,number('-')),advance);
    codegen::kernel(kernel::string_at,{norm,len,ch})(builder);
    builder.emit(op::increment_temporary_to_accumulator,{length});
    builder.bind(advance); builder.emit(op::increment_temporary_to_accumulator,{raw_index}); builder.branch(op::jump,copy);
    builder.bind(exact);
    codegen::kernel(kernel::string_at,{norm,len,number(0)})(builder);
    set_temporary(builder,record,procedure(lexicon.lookup,{norm})); branch_if(builder,temporary(record),found);
    set_temporary(builder,suffix_index,number(0));
    builder.bind(suffix);
    branch_if(builder,binary(op::less_unsigned,temporary(suffix_index),number(tables.suffix_count)),numeric,false);
    set_temporary(builder,suffix_offset,binary(op::multiply,temporary(suffix_index),number(suffix_record_words)));
    set_temporary(builder,alternate_length,codegen::kernel(kernel::string_length,{suffix_field(alternative_field)}));
    branch_if(builder,binary(op::less_unsigned,temporary(raw_index),temporary(alternate_length)),next_suffix);
    set_temporary(builder,stem_length,binary(op::subtract,temporary(raw_index),temporary(alternate_length)));
    branch_if(builder,codegen::kernel(kernel::string_compare,
        {binary(op::add,raw,temporary(stem_length)),suffix_field(alternative_field)}),next_suffix);
    branch_if(builder,binary(op::less_unsigned,
        binary(op::add,temporary(stem_length),codegen::kernel(kernel::string_length,{suffix_field(replacement_field)})),
        number(parser_word_bytes)),next_suffix,false);
    codegen::kernel(kernel::string_copy,{output,raw,temporary(stem_length)})(builder);
    codegen::kernel(kernel::string_copy,{binary(op::add,output,temporary(stem_length)),suffix_field(replacement_field)})(builder);
    set_temporary(builder,record,procedure(lexicon.lookup,{output})); branch_if(builder,temporary(record),next_suffix,false);
    branch_if(builder,binary(op::bitwise_and,procedure(lexicon.word_class,{temporary(record)}),suffix_field(mask_field)),next_suffix,false);
    set_local(builder,word_class,suffix_field(result_class_field));
    set_local(builder,group,procedure(lexicon.group,{temporary(record)})); builder.branch(op::jump,success);
    builder.bind(next_suffix); builder.emit(op::increment_temporary_to_accumulator,{suffix_index}); builder.branch(op::jump,suffix);
    builder.bind(found); resolve_record(); builder.branch(op::jump,success);
    builder.bind(numeric); branch_if(builder,len,failure,false); set_temporary(builder,raw_index,number(0));
    builder.bind(digit);
    set_temporary(builder,character,codegen::kernel(kernel::string_at,{norm,temporary(raw_index)}));
    const auto numeric_match = builder.label(); branch_if(builder,ch,numeric_match,false);
    branch_if(builder,binary(op::less_unsigned,ch,number('0')),failure);
    branch_if(builder,binary(op::less_unsigned,ch,number('9'+1)),failure,false);
    builder.emit(op::increment_temporary_to_accumulator,{raw_index}); builder.branch(op::jump,digit);
    builder.bind(numeric_match); set_local(builder,word_class,number(numeric_word_class)); set_local(builder,group,number(numeric_word_group));
    builder.bind(success); return_value(builder,number(1));
    builder.bind(failure); return_value(builder,number(0));
    return {entry,word_class,group};
}
} // namespace scimate::parser_detail
