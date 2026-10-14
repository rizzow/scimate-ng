// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_said_lookup.hpp"

namespace scimate::parser_detail {
// The early SCI1 volume's stored-size field includes four bytes in addition to
// the payload. A page must satisfy both the script and container limits.
inline constexpr std::size_t lookup_page_payload_limit = sci_vm::max_script_address-2*sci_vm::word_bytes;
struct SaidPage { Bytes payload; std::size_t specifications; };
inline std::size_t said_node_count(const SaidNode& tree) {
    std::vector<const SaidNode*> pending{&tree};
    std::size_t count = 0;
    while (!pending.empty()) {
        const auto* node = pending.back(); pending.pop_back(); ++count;
        for (const auto& child : node->children) pending.push_back(&child);
    }
    return count;
}
inline std::vector<SaidPage> write_said_pages(const std::vector<Bytes>& input,
                                           std::size_t limit = lookup_page_payload_limit) {
    if (limit > lookup_page_payload_limit) invalid_said("lookup page limit exceeds container capacity");
    ScriptBuilder empty;
    const auto empty_lookup = emit_said_lookup(empty,{}); empty.export_procedure(empty_lookup.procedure);
    // A populated table replaces the empty lookup's one dummy local.
    const auto overhead = empty.serialized_size()-sci_vm::word_bytes;
    if (limit < overhead) invalid_said("lookup page limit is smaller than the runtime");
    std::set<Bytes> unique(input.begin(),input.end());
    std::vector<SaidPage> pages;
    std::vector<Bytes> current;
    auto size = overhead;
    const auto flush = [&] {
        if (current.empty()) return;
        ScriptBuilder builder;
        const auto lookup = emit_said_lookup(builder,current); builder.export_procedure(lookup.procedure);
        if (builder.serialized_size() != size) invalid_said("lookup page size calculation disagrees with serialization");
        pages.push_back({builder.finish(),current.size()}); current.clear(); size = overhead;
    };
    for (const auto& specification : unique) {
        const auto tokens = tokenize_said(specification,0,specification.size());
        if (tokens.bytes_consumed != specification.size()) invalid_said("trailing bytes in lookup specification");
        const auto tree = SaidCompiler(tokens.tokens).compile();
        constexpr unsigned table_record_words = 4, null_node_padding_words = 1;
        const auto growth = sci_vm::word_bytes*(table_record_words+specification.size()+null_node_padding_words+
            said_node_count(tree)*tree_field::words);
        if (growth > limit-overhead) invalid_said("single Said specification exceeds lookup page capacity");
        if (growth > limit-size) flush();
        current.push_back(specification); size += growth;
    }
    flush();
    return pages;
}
inline ScriptBuilder::Label emit_said_page_dispatch(ScriptBuilder& builder, const std::vector<unsigned>& scripts) {
    using namespace codegen;
    namespace op = sci_vm::opcode;
    const auto entry = builder.label(); builder.bind(entry); builder.emit(op::allocate_temporaries,{1});
    for (const auto script : scripts) {
        const auto next = builder.label();
        set_temporary(builder,0,external_procedure(script,0,{parameter(1),parameter(2)}));
        branch_if(builder,binary(op::equal,temporary(0),number(said_specification_unknown)),next);
        return_value(builder,temporary(0)); builder.bind(next);
    }
    return_value(builder,number(said_specification_unknown));
    return entry;
}
} // namespace scimate::parser_detail
