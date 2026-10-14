// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_bridges.hpp"
#include "parser_matcher.hpp"
#include <set>

namespace scimate::parser_detail {
inline constexpr unsigned said_specification_unknown = 0xffff;
struct SaidLookup { ScriptBuilder::Label procedure; unsigned count, table; };
// The two-byte prefix index narrows byte comparison to specifications sharing
// the same first group/operator. Original pointers may come from any script.
// The unknown result allows a dispatcher to consult further lookup pages.
inline SaidLookup emit_said_lookup(ScriptBuilder& builder, const std::vector<Bytes>& input) {
    namespace op = sci_vm::opcode;
    using namespace codegen;
    constexpr unsigned record_words = 4, key_field = 0, length_field = 1, bytes_field = 2, root_field = 3;
    std::set<Bytes> unique(input.begin(),input.end());
    struct Specification { Bytes bytes; SaidNode tree; };
    std::vector<Specification> specifications;
    for (const auto& bytes : unique) {
        const auto tokens = tokenize_said(bytes,0,bytes.size());
        if (tokens.bytes_consumed != bytes.size()) invalid_said("trailing bytes in lookup specification");
        if (bytes.size() < 2) invalid_said("lookup specification has no prefix");
        specifications.push_back({bytes,SaidCompiler(tokens.tokens).compile()});
    }
    // A single page must fit the SCI locals/index address space; the export
    // layer splits larger collections rather than narrowing/truncating indices.
    if (specifications.size() > 8191) invalid_said("too many specifications in lookup page");
    const auto count = static_cast<unsigned>(specifications.size());
    const auto table = builder.local();
    for (unsigned i = 1; i < count*record_words; ++i) builder.local();
    for (unsigned i = 0; i < count; ++i) {
        const auto& spec = specifications[i];
        const auto start = table+i*record_words;
        builder.set_local(start+key_field,(static_cast<unsigned>(spec.bytes[0]) << 8) | spec.bytes[1]);
        builder.set_local(start+length_field,static_cast<unsigned>(spec.bytes.size()));
        const auto data = builder.local(spec.bytes[0]);
        for (std::size_t byte = 1; byte < spec.bytes.size(); ++byte) builder.local(spec.bytes[byte]);
        builder.set_local(start+bytes_field,data); builder.set_local(start+root_field,emit_said_tree(builder,spec.tree));
    }
    const auto matcher = emit_said_matcher(builder);
    constexpr unsigned low = 0, high = 1, middle = 2, key = 3, offset = 4, cursor = 5, data = 6, length = 7;
    const auto entry = builder.label(), search = builder.label(), upper = builder.label(), scan = builder.label();
    const auto compare = builder.label(), next = builder.label(), matched = builder.label(), missing = builder.label(), partial = builder.label(), no_match = builder.label();
    const auto index_field = [&](unsigned field) { return indexed_local(table+field,temporary(offset)); };
    builder.bind(entry); builder.emit(op::allocate_temporaries,{8});
    set_temporary(builder,key,binary(op::add,
        binary(op::multiply,kernel(sci_vm::sci1_kernel::string_at,{parameter(2),number(0)}),number(256)),
        kernel(sci_vm::sci1_kernel::string_at,{parameter(2),number(1)})));
    set_temporary(builder,low,number(0)); set_temporary(builder,high,number(count));
    builder.bind(search);
    branch_if(builder,binary(op::less_unsigned,temporary(low),temporary(high)),scan,false);
    set_temporary(builder,middle,binary(op::divide,binary(op::add,temporary(low),temporary(high)),number(2)));
    set_temporary(builder,offset,binary(op::multiply,temporary(middle),number(record_words)));
    branch_if(builder,binary(op::less_unsigned,index_field(key_field),temporary(key)),upper,false);
    set_temporary(builder,low,binary(op::add,temporary(middle),number(1))); builder.branch(op::jump,search);
    builder.bind(upper); set_temporary(builder,high,temporary(middle)); builder.branch(op::jump,search);
    builder.bind(scan); branch_if(builder,binary(op::less_unsigned,temporary(low),number(count)),missing,false);
    set_temporary(builder,offset,binary(op::multiply,temporary(low),number(record_words)));
    branch_if(builder,binary(op::equal,index_field(key_field),temporary(key)),missing,false);
    set_temporary(builder,data,index_field(bytes_field)); set_temporary(builder,length,index_field(length_field));
    set_temporary(builder,cursor,number(2));
    builder.bind(compare); branch_if(builder,binary(op::less_unsigned,temporary(cursor),temporary(length)),matched,false);
    branch_if(builder,binary(op::equal,kernel(sci_vm::sci1_kernel::string_at,{parameter(2),temporary(cursor)}),
        indexed_local(0,binary(op::add,temporary(data),temporary(cursor)))),next,false);
    builder.emit(op::increment_temporary_to_accumulator,{cursor}); builder.branch(op::jump,compare);
    builder.bind(next); builder.emit(op::increment_temporary_to_accumulator,{low}); builder.branch(op::jump,scan);
    builder.bind(matched);
    branch_if(builder,binary(op::equal,procedure(matcher.procedure,{parameter(1),number(tree_root_index),index_field(root_field)}),number(tree_matched)),no_match,false);
    branch_if(builder,local(matcher.partial),partial); return_value(builder,number(static_cast<unsigned>(SaidResult::full_match)));
    builder.bind(partial); return_value(builder,number(static_cast<unsigned>(SaidResult::partial_match)));
    builder.bind(no_match); return_value(builder,number(static_cast<unsigned>(SaidResult::no_match)));
    builder.bind(missing); return_value(builder,number(said_specification_unknown));
    return {entry,count,table};
}
} // namespace scimate::parser_detail
