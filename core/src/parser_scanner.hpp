// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_words.hpp"

namespace scimate::parser_detail {
inline constexpr unsigned token_character = 0x100, token_boundary = 0x200, token_hyphen = 0x400;
// SCI0's default OEM case folding. Input byte classification is explicit and
// independent of the host locale; punctuation other than space is ignored.
inline std::array<unsigned,256> scanner_character_table() {
    std::array<unsigned,256> table{};
    for (unsigned byte = 0; byte < table.size(); ++byte) {
        auto lower = byte;
        if (byte >= 'A' && byte <= 'Z') lower += 'a'-'A';
        unsigned flags = 0;
        if ((byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z') || byte >= 128) flags = token_character;
        if (byte == 0 || byte == ' ') flags = token_boundary;
        if (byte == '-') flags = token_hyphen;
        table[byte] = lower | flags;
    }
    for (const auto& pair : {std::pair{0x80u,0x87u},std::pair{0x8eu,0x84u},std::pair{0x8fu,0x86u},
                            std::pair{0x90u,0x82u},std::pair{0x92u,0x91u},std::pair{0x99u,0x94u},std::pair{0x9au,0x81u}})
        table[pair.first] = pair.second | token_character;
    return table;
}
enum class ScanFailure : unsigned { none, unknown_word, word_too_long, too_many_words, input_too_long };
struct TokenScanner {
    ScriptBuilder::Label procedure;
    unsigned count, classes, groups, word_buffer, failure, word_start;
};
inline TokenScanner emit_token_scanner(ScriptBuilder& builder, const WordResolver& resolver) {
    namespace op = sci_vm::opcode;
    namespace kernels = sci_vm::sci1_kernel;
    using namespace codegen;
    constexpr unsigned max_tokens = 64, max_input_bytes = 4096, byte_mask = 255;
    const auto count = builder.local(), failure = builder.local(), word_start = builder.local();
    const auto buffer = allocate_word_buffer(builder);
    const auto classes = builder.local(); for (unsigned i = 1; i < max_tokens; ++i) builder.local();
    const auto groups = builder.local(); for (unsigned i = 1; i < max_tokens; ++i) builder.local();
    unsigned characters = 0;
    const auto table = scanner_character_table();
    for (unsigned i = 0; i < table.size(); ++i) { const auto index = builder.local(table[i]); if (!i) characters = index; }
    constexpr unsigned cursor = 0, length = 1, character = 2, classification = 3, temporaries = 4;
    const auto entry = builder.label(), scan = builder.label(), append = builder.label(), advance = builder.label();
    const auto flush = builder.label(), finish_word = builder.label(), check_end = builder.label(), success = builder.label();
    const auto unknown = builder.label(), long_word = builder.label(), excess_words = builder.label(), long_input = builder.label();
    const auto fail = builder.label();
    const auto len = temporary(length), ch = temporary(character), word = address(buffer);
    builder.bind(entry); builder.emit(op::allocate_temporaries,{temporaries});
    set_local(builder,count,number(0)); set_local(builder,failure,number(0)); set_local(builder,word_start,number(0));
    set_temporary(builder,cursor,number(0)); set_temporary(builder,length,number(0));
    builder.bind(scan);
    branch_if(builder,binary(op::less_unsigned,temporary(cursor),number(max_input_bytes)),long_input,false);
    set_temporary(builder,classification,indexed_local(characters,kernel(kernels::string_at,{parameter(1),temporary(cursor)})));
    set_temporary(builder,character,binary(op::bitwise_and,temporary(classification),number(byte_mask)));
    branch_if(builder,binary(op::bitwise_and,temporary(classification),number(token_boundary)),flush);
    branch_if(builder,binary(op::bitwise_and,temporary(classification),number(token_character)),append);
    branch_if(builder,binary(op::bitwise_and,temporary(classification),number(token_hyphen)),advance,false);
    branch_if(builder,len,advance,false); // A hyphen may occur only after a word begins.
    builder.bind(append);
    branch_if(builder,binary(op::less_unsigned,len,number(parser_word_bytes-1)),long_word,false);
    branch_if(builder,len,finish_word);
    set_local(builder,word_start,temporary(cursor));
    builder.bind(finish_word);
    kernel(kernels::string_at,{word,len,ch})(builder);
    builder.emit(op::increment_temporary_to_accumulator,{length});
    builder.bind(advance); builder.emit(op::increment_temporary_to_accumulator,{cursor}); builder.branch(op::jump,scan);
    builder.bind(flush); branch_if(builder,len,check_end,false);
    kernel(kernels::string_at,{word,len,number(0)})(builder);
    branch_if(builder,procedure(resolver.procedure,{word}),unknown,false);
    branch_if(builder,binary(op::less_unsigned,local(count),number(max_tokens)),excess_words,false);
    set_indexed_local(builder,classes,local(count),local(resolver.word_class));
    set_indexed_local(builder,groups,local(count),local(resolver.group));
    set_local(builder,count,binary(op::add,local(count),number(1))); set_temporary(builder,length,number(0));
    builder.bind(check_end); branch_if(builder,ch,success,false); builder.branch(op::jump,advance);
    for (const auto& error : {std::pair{unknown,ScanFailure::unknown_word},std::pair{long_word,ScanFailure::word_too_long},
                             std::pair{excess_words,ScanFailure::too_many_words},std::pair{long_input,ScanFailure::input_too_long}}) {
        builder.bind(error.first); set_local(builder,failure,number(static_cast<unsigned>(error.second))); builder.branch(op::jump,fail);
    }
    builder.bind(fail);
    // Keep a safe error string even when failure occurs before a word boundary.
    kernel(kernels::string_at,{word,len,number(0)})(builder);
    set_local(builder,count,number(0)); return_value(builder,number(0));
    builder.bind(success); return_value(builder,number(1));
    return {entry,count,classes,groups,buffer,failure,word_start};
}
} // namespace scimate::parser_detail
