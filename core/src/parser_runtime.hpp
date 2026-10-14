// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_input.hpp"
#include "parser_pages.hpp"

namespace scimate::parser_detail {
// Stable exports used by the source-script rewrite. Synonym selection remains
// the caller's responsibility; append copies a proven loaded script's pairs.
enum class ParserExport : unsigned { parse, said, clear_synonyms, append_synonyms };
struct ParserRuntimeScript { std::uint16_t number; Bytes payload; };
struct ParserRuntime {
    std::uint16_t entry_script;
    std::vector<ParserRuntimeScript> scripts;
};
inline ParserRuntime write_parser_runtime(const ParserInput& input) {
    auto pages = write_said_pages(input.specifications);
    const auto numbers = allocate_parser_scripts(input,pages.size()+1);
    std::vector<unsigned> lookup_scripts(numbers.begin()+1,numbers.end());
    ScriptBuilder builder;
    const auto pipeline = emit_parser_pipeline(builder,input.words,input.grammar,input.suffixes);
    const auto lookup = emit_said_page_dispatch(builder,lookup_scripts);
    const auto bridges = emit_parser_bridges(builder,pipeline,input.selectors,lookup);
    builder.export_procedure(bridges.parse);
    builder.export_procedure(bridges.said);
    builder.export_procedure(pipeline.synonyms.clear);
    builder.export_procedure(pipeline.synonyms.append);
    if (builder.serialized_size() > lookup_page_payload_limit)
        throw std::runtime_error("portable parser: runtime exceeds early SCI1 container capacity");
    ParserRuntime runtime{numbers.front(),{}};
    runtime.scripts.push_back({numbers.front(),builder.finish()});
    for (std::size_t i = 0; i < pages.size(); ++i)
        runtime.scripts.push_back({numbers[i+1],std::move(pages[i].payload)});
    return runtime;
}
} // namespace scimate::parser_detail
