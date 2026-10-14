// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_input.hpp"
#include "parser_pages.hpp"
#include "parser_lexicon_pages.hpp"
#include <optional>

namespace scimate::parser_detail {
// Stable exports used by the source-script rewrite. Synonym selection remains
// the caller's responsibility; append copies a proven loaded script's pairs.
enum class ParserExport : unsigned { parse, said, clear_synonyms, append_synonyms, set_synonyms };
struct ParserRuntimeScript { std::uint16_t number; Bytes payload; };
struct ParserRuntime {
    std::uint16_t entry_script;
    std::vector<ParserRuntimeScript> scripts;
};
inline ParserRuntime write_parser_runtime(const ParserInput& input, std::optional<SynonymSelectors> synonyms = std::nullopt) {
    auto pages = write_said_pages(input.specifications);
    auto lexicon_pages = write_lexicon_pages(input.words);
    const auto numbers = allocate_parser_scripts(input,pages.size()+lexicon_pages.size()+1);
    const auto lexicon_begin = numbers.begin()+1+pages.size();
    std::vector<unsigned> lookup_scripts(numbers.begin()+1,lexicon_begin);
    std::vector<unsigned> lexicon_scripts(lexicon_begin,numbers.end());
    ScriptBuilder builder;
    const auto lexicon = emit_lexicon_page_dispatch(builder,lexicon_pages,lexicon_scripts);
    builder.account_component("lexicon dispatch and resource headers");
    const auto pipeline = emit_parser_pipeline(builder,input.words,input.grammar,input.suffixes,&lexicon);
    const auto lookup = emit_said_page_dispatch(builder,lookup_scripts);
    const auto bridges = emit_parser_bridges(builder,pipeline,input.selectors,lookup);
    builder.export_procedure(bridges.parse);
    builder.export_procedure(bridges.said);
    builder.export_procedure(pipeline.synonyms.clear);
    builder.export_procedure(pipeline.synonyms.append);
    if (synonyms) builder.export_procedure(emit_synonym_selection(builder,pipeline.synonyms,*synonyms,input.synonyms,input.global_count));
    builder.account_component("Said dispatch, callbacks, exports and synonym selection");
    if (builder.serialized_size() > lookup_page_payload_limit)
        throw std::runtime_error("portable parser: entry resource requires "+std::to_string(builder.serialized_size())+
            " bytes; early SCI1 limit is "+std::to_string(lookup_page_payload_limit)+" bytes; "+builder.size_report());
    ParserRuntime runtime{numbers.front(),{}};
    runtime.scripts.push_back({numbers.front(),builder.finish()});
    for (std::size_t i = 0; i < pages.size(); ++i)
        runtime.scripts.push_back({numbers[i+1],std::move(pages[i].payload)});
    for (std::size_t i = 0; i < lexicon_pages.size(); ++i)
        runtime.scripts.push_back({numbers[i+1+pages.size()],std::move(lexicon_pages[i].payload)});
    return runtime;
}
} // namespace scimate::parser_detail
