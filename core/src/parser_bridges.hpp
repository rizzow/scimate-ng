// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_pipeline.hpp"

namespace scimate::parser_detail {
struct ParserSelectors { unsigned claimed, word_fail, syntax_fail; };
enum class SaidResult : unsigned { no_match, full_match, partial_match };
struct ParserBridges { ScriptBuilder::Label parse, said; unsigned event, valid, message_buffer; };
// Said lookup ABI: (tree arena, original Said pointer) -> SaidResult. Selector
// numbers are resolved from the source vocabulary by the export layer, never
// guessed from a title or a particular compiler's selector ordering.
inline ParserBridges emit_parser_bridges(ScriptBuilder& builder, const ParserPipeline& pipeline,
    ParserSelectors selectors, ScriptBuilder::Label said_lookup) {
    namespace op = sci_vm::opcode;
    using namespace codegen;
    const auto event = builder.local(), valid = builder.local(), message = allocate_word_buffer(builder);
    const auto parse = builder.label(), said = builder.label();
    const auto game = kernel(sci_vm::sci1_kernel::script_id,{number(0),number(0)});
    {
        const auto failure = builder.label(), empty = builder.label(), syntax = builder.label();
        builder.bind(parse); set_local(builder,event,parameter(2)); set_local(builder,valid,number(0));
        branch_if(builder,procedure(pipeline.procedure,{parameter(1)}),failure,false);
        set_local(builder,valid,number(1)); send(local(event),selectors.claimed,{number(0)})(builder);
        return_value(builder,number(1));
        builder.bind(failure); send(local(event),selectors.claimed,{number(1)})(builder);
        branch_if(builder,binary(op::equal,local(pipeline.tokens.failure),number(static_cast<unsigned>(ScanFailure::unknown_word))),syntax,false);
        kernel(sci_vm::sci1_kernel::string_copy,{address(message),address(pipeline.tokens.word_buffer)})(builder);
        send(game,selectors.word_fail,{address(message),parameter(1)})(builder); return_value(builder,number(1));
        builder.bind(syntax);
        // Empty input has no syntax callback. Scanner capacity failures must
        // still be reported even though they clear the token count.
        const auto report = builder.label(); branch_if(builder,local(pipeline.tokens.failure),report);
        branch_if(builder,local(pipeline.tokens.count),empty,false);
        builder.bind(report); send(game,selectors.syntax_fail,{address(message),parameter(1)})(builder);
        builder.emit(op::return_); // Preserve the callback's accumulator result.
        builder.bind(empty); return_value(builder,number(0));
    }
    {
        constexpr unsigned result = 0;
        const auto missing = builder.label(), partial = builder.label();
        builder.bind(said); builder.emit(op::allocate_temporaries,{1});
        branch_if(builder,parameter(1),missing,false);
        branch_if(builder,local(event),missing,false); branch_if(builder,local(valid),missing,false);
        branch_if(builder,send(local(event),selectors.claimed),missing);
        set_temporary(builder,result,procedure(said_lookup,{local(pipeline.tree.arena),parameter(1)}));
        branch_if(builder,binary(op::equal,temporary(result),number(static_cast<unsigned>(SaidResult::partial_match))),partial);
        branch_if(builder,binary(op::equal,temporary(result),number(static_cast<unsigned>(SaidResult::full_match))),missing,false);
        send(local(event),selectors.claimed,{number(1)})(builder);
        builder.bind(partial); return_value(builder,number(1));
        builder.bind(missing); return_value(builder,number(0));
    }
    return {parse,said,event,valid,message};
}
} // namespace scimate::parser_detail
