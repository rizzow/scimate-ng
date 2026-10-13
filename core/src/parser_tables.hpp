// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_bytecode.hpp"
#include <array>

namespace scimate::parser_detail {
namespace grammar_tag {
inline constexpr unsigned last_word_storage = 0x140, class_mask = 0x146;
inline constexpr unsigned group_mask = 0x14d, stored_word = 0x154;
}
inline constexpr std::size_t grammar_record_bytes = 20, grammar_record_words = 10;
using GrammarRecord = std::array<unsigned,grammar_record_words>;
struct GrammarTable {
    std::vector<GrammarRecord> records;
    unsigned root_id() const { return records.at(0)[0]; }
    unsigned start_id() const { return records.at(0)[2]; }
};
[[noreturn]] inline void invalid_parser_table(const char* message) {
    throw std::runtime_error(std::string("portable parser tables: ")+message);
}
inline unsigned table_word(const Bytes& bytes, std::size_t offset, bool big_endian = false) {
    if (offset > bytes.size() || bytes.size()-offset < sci_vm::word_bytes) invalid_parser_table("truncated word");
    return big_endian ? (static_cast<unsigned>(bytes[offset]) << 8) | bytes[offset+1] :
        bytes[offset] | (static_cast<unsigned>(bytes[offset+1]) << 8);
}
inline GrammarTable read_grammar_table(const Bytes& bytes) {
    if (bytes.empty() || bytes.size()%grammar_record_bytes) invalid_parser_table("grammar size is not a multiple of 20 bytes");
    GrammarTable table;
    for (std::size_t offset = 0; offset < bytes.size(); offset += grammar_record_bytes) {
        GrammarRecord record{};
        for (std::size_t word = 0; word < record.size(); ++word) record[word] = table_word(bytes,offset+word*sci_vm::word_bytes);
        // A final empty record is a terminator, not a grammar production.
        if (record[0] == 0 && offset+grammar_record_bytes == bytes.size()) {
            for (const auto value : record) if (value) invalid_parser_table("nonempty grammar terminator");
            break;
        }
        if (!record[0]) invalid_parser_table("zero grammar rule ID");
        table.records.push_back(record);
    }
    if (table.records.size() < 2 || !table.start_id()) invalid_parser_table("missing grammar start rule");
    std::set<unsigned> ids;
    for (std::size_t i = 1; i < table.records.size(); ++i) ids.insert(table.records[i][0]);
    if (!ids.count(table.start_id())) invalid_parser_table("undefined grammar start rule");
    for (std::size_t i = 1; i < table.records.size(); ++i) {
        const auto& record = table.records[i];
        bool terminated = false;
        for (std::size_t field = 1; field < record.size(); field += 2) {
            const auto tag = record[field];
            if (!tag) { terminated = true; break; }
            if (field+1 == record.size()) invalid_parser_table("unpaired grammar tag");
            if (tag == grammar_tag::class_mask || tag == grammar_tag::group_mask || tag == grammar_tag::stored_word) continue;
            if (tag <= grammar_tag::last_word_storage) invalid_parser_table("unsupported grammar tag");
            if (!ids.count(record[field+1])) invalid_parser_table("undefined grammar nonterminal");
        }
        if (!terminated || !record[1]) invalid_parser_table("empty or unterminated grammar production");
    }
    return table;
}
struct SuffixRule {
    std::string alternative, replacement;
    unsigned result_class, class_mask;
};
inline std::vector<SuffixRule> read_suffix_table(const Bytes& bytes) {
    std::vector<SuffixRule> rules;
    if (bytes.empty()) return rules;
    std::size_t cursor = 0;
    const auto string = [&]() {
        const auto start = cursor;
        while (cursor < bytes.size() && bytes[cursor]) ++cursor;
        if (cursor == bytes.size()) invalid_parser_table("unterminated suffix string");
        std::string value(bytes.begin()+start,bytes.begin()+cursor); ++cursor; return value;
    };
    while (cursor < bytes.size()) {
        if (bytes[cursor] != '*') invalid_parser_table("missing suffix wildcard");
        ++cursor;
        if (cursor+1 >= bytes.size()) invalid_parser_table("truncated suffix record");
        // Legacy resources terminate with an empty suffix and a 0xffff class.
        if (bytes[cursor] == 0 && table_word(bytes,cursor+1,true) == 0xffff) break;
        SuffixRule rule;
        rule.alternative = string();
        rule.result_class = table_word(bytes,cursor,true); cursor += sci_vm::word_bytes;
        if (cursor >= bytes.size() || bytes[cursor] != '*') invalid_parser_table("missing replacement wildcard");
        ++cursor; rule.replacement = string();
        rule.class_mask = table_word(bytes,cursor,true); cursor += sci_vm::word_bytes;
        rules.push_back(std::move(rule));
    }
    return rules;
}
struct ParserTableLayout {
    unsigned grammar_start, grammar_count, start_id, root_id;
    unsigned suffix_start, suffix_count;
};
inline ParserTableLayout emit_parser_tables(ScriptBuilder& builder, const GrammarTable& grammar,
                                           const std::vector<SuffixRule>& suffixes) {
    unsigned grammar_start = 0;
    for (std::size_t i = 0; i < grammar.records.size(); ++i)
        for (std::size_t field = 0; field < grammar_record_words; ++field) {
            const auto index = builder.local(grammar.records[i][field]);
            if (i == 0 && field == 0) grammar_start = index;
        }
    unsigned suffix_start = 0;
    for (std::size_t i = 0; i < suffixes.size(); ++i) {
        const auto& suffix = suffixes[i];
        const auto index = builder.string_local(suffix.alternative);
        if (i == 0) suffix_start = index;
        builder.string_local(suffix.replacement); builder.local(suffix.result_class); builder.local(suffix.class_mask);
    }
    return {grammar_start,static_cast<unsigned>(grammar.records.size()),grammar.start_id(),grammar.root_id(),
        suffix_start,static_cast<unsigned>(suffixes.size())};
}
} // namespace scimate::parser_detail
