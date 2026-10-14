// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_pages.hpp"
#include "parser_lexicon.hpp"

namespace scimate::parser_detail {
enum class LexiconExport : unsigned { lookup, word_class, group };
struct LexiconPage { Bytes payload; unsigned count; };
inline std::vector<LexiconPage> write_lexicon_pages(const std::vector<Word>& source,
                                                 std::size_t limit = lookup_page_payload_limit) {
    const auto emit = [](ScriptBuilder& builder, const std::vector<Word>& words) {
        const auto procedures = emit_lexicon(builder,words);
        for (const auto label : {procedures.lookup,procedures.word_class,procedures.group}) builder.export_procedure(label);
    };
    ScriptBuilder probe; emit(probe,{{"a",0,0}});
    // Three locals, one relocation, and the NUL-terminated spelling per word.
    constexpr unsigned record_bytes = 4*sci_vm::word_bytes+1;
    const auto overhead = probe.serialized_size()-(record_bytes+1);
    if (limit > lookup_page_payload_limit || limit <= overhead)
        throw std::runtime_error("portable parser: invalid lexicon page capacity");
    std::map<std::string,Word> unique;
    for (const auto& word : source) unique[word.text] = word;
    if (unique.size() >= sci_vm::max_script_address)
        throw std::runtime_error("portable parser: vocabulary exceeds global word-index range");
    std::vector<LexiconPage> pages;
    std::vector<Word> current;
    auto size = overhead;
    const auto flush = [&] {
        if (current.empty()) return;
        ScriptBuilder builder; emit(builder,current);
        if (builder.serialized_size() != size)
            throw std::runtime_error("portable parser: lexicon page size disagrees with serialization");
        pages.push_back({builder.finish(),static_cast<unsigned>(current.size())});
        current.clear(); size = overhead;
    };
    for (const auto& entry : unique) {
        const auto growth = record_bytes+entry.first.size();
        if (growth > limit-overhead) throw std::runtime_error("portable parser: word exceeds lexicon page capacity");
        if (growth > limit-size) flush();
        current.push_back(entry.second); size += growth;
    }
    flush(); return pages;
}
inline LexiconProcedures emit_lexicon_page_dispatch(ScriptBuilder& builder,
    const std::vector<LexiconPage>& pages, const std::vector<unsigned>& scripts) {
    using namespace codegen; namespace op = sci_vm::opcode;
    if (pages.size() != scripts.size()) throw std::runtime_error("portable parser: lexicon page IDs disagree");
    const auto lookup = builder.label(), word_class = builder.label(), group = builder.label();
    builder.bind(lookup); builder.emit(op::allocate_temporaries,{1});
    unsigned count = 0;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto next = builder.label();
        set_temporary(builder,0,external_procedure(scripts[i],static_cast<unsigned>(LexiconExport::lookup),{parameter(1)}));
        branch_if(builder,temporary(0),next,false);
        return_value(builder,binary(op::add,temporary(0),number(count)));
        builder.bind(next); count += pages[i].count;
    }
    return_value(builder,number(0));
    for (const auto& property : {std::pair{word_class,LexiconExport::word_class},std::pair{group,LexiconExport::group}}) {
        const auto invalid = builder.label(); builder.bind(property.first);
        branch_if(builder,parameter(1),invalid,false);
        unsigned offset = 0;
        for (std::size_t i = 0; i < pages.size(); ++i) {
            const auto next = builder.label();
            branch_if(builder,binary(op::less_equal_unsigned,parameter(1),number(offset+pages[i].count)),next,false);
            return_value(builder,external_procedure(scripts[i],static_cast<unsigned>(property.second),
                {binary(op::subtract,parameter(1),number(offset))}));
            builder.bind(next); offset += pages[i].count;
        }
        builder.bind(invalid); return_value(builder,number(0));
    }
    return {lookup,word_class,group,0,count};
}
} // namespace scimate::parser_detail
