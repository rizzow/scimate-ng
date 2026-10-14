// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "scimate/sci1.hpp"
#include "scimate/assets.hpp"
#include "scimate/picture.hpp"
#include "scimate/view.hpp"
#include "../core/src/script_internal.hpp"
#include "../core/src/parser_lexicon.hpp"
#include "../core/src/parser_tables.hpp"
#include "../core/src/parser_grammar.hpp"
#include "../core/src/parser_scanner.hpp"
#include "../core/src/parser_sentence.hpp"
#include "../core/src/parser_said.hpp"
#include "../core/src/parser_matcher.hpp"
#include "../core/src/parser_pipeline.hpp"
#include "../core/src/parser_bridges.hpp"
#include "../core/src/parser_said_lookup.hpp"
#include "../core/src/parser_pages.hpp"
#include "../core/src/parser_input.hpp"
#include "../core/src/parser_runtime.hpp"
#include "../core/src/migration_bridges.hpp"
#include "../core/src/migration_identity.hpp"
#include <iostream>

namespace {
using namespace fixture;
void patch(Bytes& bytes, std::size_t at, unsigned value) {
    bytes.at(at) = static_cast<std::uint8_t>(value); bytes.at(at+1) = static_cast<std::uint8_t>(value >> 8);
}
Bytes msb(const std::vector<std::pair<unsigned,unsigned>>& codes) {
    Bytes bytes;
    unsigned bits = 0;
    for (auto item : codes) for (unsigned i = item.second; i > 0; --i) {
        if (bits%8 == 0) bytes.push_back(0);
        bytes.back() |= static_cast<std::uint8_t>(((item.first >> (i-1)) & 1u) << (7-bits%8));
        ++bits;
    }
    return bytes;
}
Resource resource(ResourceType type, unsigned number, Bytes payload) {
    return {{{type,static_cast<std::uint16_t>(number)},0,0},0,std::move(payload)};
}
Game sample() {
    Game game;
    Bytes bytes{7,0,8,0,1,0,0,0}; // Export 0 patched below.
    put(bytes,2,2); put(bytes,11,2);
    bytes.insert(bytes.end(), {0x78,0x39,0,0x43,0x2d,2,0x48}); // SaveGame kernel, remapped below.
    put(bytes,5,2); put(bytes,9,2);
    auto name = bytes.size(); bytes.insert(bytes.end(),{'D','E','M','O',0});
    put(bytes,1,2); put(bytes,24,2);
    put(bytes,0x1234,2); put(bytes,0,2); put(bytes,0,2); put(bytes,4,2);
    auto pointer = bytes.size(); patch(bytes,6,pointer);
    put(bytes,0,2); put(bytes,0xffff,2); put(bytes,0,2); put(bytes,name,2);
    put(bytes,0,2); put(bytes,0,2); // method count and separator
    put(bytes,8,2); put(bytes,8,2); put(bytes,1,2); put(bytes,pointer+6,2); put(bytes,0,2);
    game.resources.push_back(resource(ResourceType::script,0,std::move(bytes)));
    // Synthetic selector vocabulary; no commercial game data.
    game.resources.push_back(resource(ResourceType::vocab,997,{0,0,4,0,4,0,'n','a','m','e'}));
    // FOpen/FPuts/FGets/FClose and a remapped kernel in another script.
    Bytes code{0x43,0x29,4,0x43,0x2a,4,0x43,0x2b,6,0x43,0x2c,2,0x43,0x40,0,0x48};
    Bytes other; put(other,2,2); put(other,code.size()+4,2);
    other.insert(other.end(),code.begin(),code.end()); put(other,0,2);
    game.resources.push_back(resource(ResourceType::script,20,std::move(other)));
    return game;
}
void tests() {
    const Bytes nonwildcard_suffix_fixture{0,'s',0,0x01,0x00,0,0,0x08,0x00,0,0,0xff};
    const auto suffix_rules = parser_detail::read_suffix_table(nonwildcard_suffix_fixture);
    require(suffix_rules.size() == 1 && suffix_rules.front().alternative == "s" &&
            suffix_rules.front().replacement.empty() && suffix_rules.front().result_class == 0x100 &&
            suffix_rules.front().class_mask == 0x800,
            "Suffix decoding requires wildcard separators or lost big-endian classes");
    require(parser_detail::read_suffix_table({0,0,0xff}).empty(),"Suffix terminator requires a complete dummy rule");
    fails([&] { parser_detail::read_suffix_table({0,'s',0,1}); },"truncated word");
    const auto synonym_pairs = parser_detail::read_synonym_pairs({1,0,2,0,2,0,3,0});
    require(synonym_pairs.size() == 2 && synonym_pairs[0].original == 1 && synonym_pairs[0].replacement == 2 &&
            synonym_pairs[1].original == 2 && synonym_pairs[1].replacement == 3,
            "Synonym word order, byte order or chained replacements changed");
    require(parser_detail::read_synonym_pairs({1,0,2}).empty(),"Partial synonym pair was treated as an alias");
    for (unsigned tail = 1; tail < 4; ++tail) {
        Bytes padded_synonyms{1,0,2,0}; padded_synonyms.insert(padded_synonyms.end(),tail,0xff);
        const auto aliases = parser_detail::read_synonym_pairs(padded_synonyms);
        require(aliases.size() == 1 && aliases.front().original == 1 && aliases.front().replacement == 2,
                "Trailing synonym bytes changed complete aliases");
    }
    const Bytes said_fixture{0,12,0xf2,0xf5,0,21,0xf6,0xf9,0xff};
    const auto said_tokens = parser_detail::tokenize_said(said_fixture,0,said_fixture.size());
    const auto said_tree = parser_detail::compile_said(said_fixture,0,said_fixture.size());
    require(said_tokens.bytes_consumed == said_fixture.size() && said_tokens.tokens[0].group && said_tokens.tokens[0].value == 12,
            "Said group byte order or terminator consumption changed");
    require(said_tree.major == 0x141 && said_tree.minor == 0x13f && said_tree.children.size() == 3 &&
            said_tree.children[0].minor == 0x149 && said_tree.children[1].major == 0x142 &&
            said_tree.children[1].children[0].children[0].major == 0x152 && said_tree.children[2].major == 0x14b,
            "Said sentence parts, optional term or partial match marker were lost");
    const Bytes reference_said{0,12,0xf0,0,13,0xf8,0,21,0xff};
    const auto reference_tree = parser_detail::compile_said(reference_said,0,reference_said.size());
    require(reference_tree.children[0].children.size() == 2 &&
            reference_tree.children[0].children[0].children.size() == 2 && reference_tree.children[0].children[1].major == 0x144,
            "Said alternatives or reference semantics were flattened");
    fails([&] { parser_detail::tokenize_said(Bytes{0},0,1); },"truncated");
    fails([&] { parser_detail::compile_said(Bytes{0,12,0xf1,0,21,0xff},0,6); },"invalid");
    parser_detail::ScriptBuilder matcher_builder;
    const auto matcher_tree = parser_detail::emit_said_tree(matcher_builder,said_tree);
    const auto matcher = parser_detail::emit_said_matcher(matcher_builder);
    matcher_builder.export_procedure(matcher.procedure);
    const auto matcher_bytes = matcher_builder.finish();
    const auto matcher_script = script_detail::parse(matcher_bytes);
    require(matcher_tree != 0 && matcher_script.exports.size() == 1,"Said matcher tree root or procedure export is invalid");
    bool has_tree_memory = false;
    for (const auto& instruction : matcher_script.instructions) if (instruction.opcode == sci_vm::opcode::call_kernel) {
        require(instruction.operands[0] == 0x72,"Said matcher depends on a native parser or game-specific kernel");
        has_tree_memory = true;
    }
    require(has_tree_memory,"Said matcher cannot read the parsed tree arena");
    for (const auto& section : matcher_script.sections) if (section.type == 10) {
        const auto root = section.start+4+matcher_tree*2;
        require(script_detail::word(matcher_bytes,root,section.end) == 0x141 &&
                script_detail::word(matcher_bytes,root+2,section.end) == 0x13f &&
                script_detail::word(matcher_bytes,root+8,section.end) != 0,
                "Serialized Said tree lost its major/minor tags or child index");
    }
    // The leading rule wraps both predicate and subject trees. Normalization
    // consumes the verb first without losing the subject's pending nonterminal.
    const parser_detail::GrammarTable nested_grammar{{
        {0x555,0x141,0x123,0,0,0,0,0,0,0},
        {0x123,0x141,0x124,0x142,0x125,0,0,0,0,0},
        {0x124,0x146,0x800,0,0,0,0,0,0,0},
        {0x125,0x14d,21,0,0,0,0,0,0,0}
    }};
    const auto normalized_grammar = parser_detail::compile_grammar(nested_grammar);
    using GrammarKind = parser_detail::GrammarSymbolKind;
    const std::vector<parser_detail::GrammarSymbol> expected_production{
        {GrammarKind::open},{GrammarKind::tag,0x141},{GrammarKind::tag,0x124},{GrammarKind::class_mask,0x800},
        {GrammarKind::close},{GrammarKind::open},{GrammarKind::tag,0x142},{GrammarKind::tag,0x125},
        {GrammarKind::nonterminal,0x125},{GrammarKind::close}
    };
    require(normalized_grammar.productions.size() == 3 && normalized_grammar.productions.back().id == 0x123 &&
            normalized_grammar.productions.back().symbols == expected_production,
            "portable grammar normalization lost semantic tags or pending input");
    parser_detail::ScriptBuilder grammar_builder;
    const auto grammar_layout = parser_detail::emit_compiled_grammar(grammar_builder,normalized_grammar);
    const auto grammar_entry = parser_detail::emit_rule_selector(grammar_builder,grammar_layout);
    grammar_builder.export_procedure(grammar_entry);
    const auto generated_grammar = script_detail::parse(grammar_builder.finish());
    require(grammar_layout.count_local == 0 && grammar_layout.offsets_local == 1 && generated_grammar.exports.size() == 1,
            "portable grammar table was not emitted as SCI locals");
    bool uses_class_group_masks = false;
    for (const auto& instruction : generated_grammar.instructions) {
        require(instruction.opcode != sci_vm::opcode::call_kernel,"grammar selector unexpectedly depends on native parser kernels");
        if (instruction.opcode == sci_vm::opcode::bitwise_and) uses_class_group_masks = true;
    }
    require(uses_class_group_masks,"grammar selector lost class/group bitmask matching");
    fails([&] { parser_detail::compile_grammar(nested_grammar,{2,1024}); },"limits");
    auto recursive_grammar = nested_grammar;
    recursive_grammar.records.insert(recursive_grammar.records.begin()+2,
        {0x124,0x141,0x124,0,0,0,0,0,0,0});
    fails([&] { parser_detail::compile_grammar(recursive_grammar,{16,1024}); },"limits");
    parser_detail::ScriptBuilder pipeline_builder;
    const auto pipeline = parser_detail::emit_parser_pipeline(pipeline_builder,
        {{"look",0x800,12},{"coin",0x100,21}},nested_grammar,{});
    const auto bridge_lookup = parser_detail::emit_said_lookup(pipeline_builder,{said_fixture,reference_said,said_fixture});
    require(bridge_lookup.count == 2,"Said lookup did not deduplicate identical specifications");
    const auto bridges = parser_detail::emit_parser_bridges(pipeline_builder,pipeline,{17,23,31},bridge_lookup.procedure);
    pipeline_builder.export_procedure(pipeline.procedure);
    pipeline_builder.export_procedure(bridges.parse); pipeline_builder.export_procedure(bridges.said);
    const auto pipeline_bytes = pipeline_builder.finish();
    const auto pipeline_script = script_detail::parse(pipeline_bytes);
    bool has_tree_capacity = false;
    for (const auto& instruction : pipeline_script.instructions) {
        if (instruction.opcode == sci_vm::opcode::load_immediate && instruction.operands[0] == 6002) has_tree_capacity = true;
        if (instruction.opcode == sci_vm::opcode::call_kernel)
            require(instruction.operands[0] != 0x24 && instruction.operands[0] != 0x25,"portable parser pipeline calls native parser kernels");
    }
    require(pipeline_script.exports.size() == 3 && has_tree_capacity && pipeline.tree.arena != pipeline.sentence.result,
            "portable parser pipeline did not separate candidate and tree arenas");
    bool sends_callbacks = false, sends_claimed = false;
    unsigned word_callbacks = 0, syntax_callbacks = 0;
    for (std::size_t i = 0; i < pipeline_script.instructions.size(); ++i) {
        const auto& instruction = pipeline_script.instructions[i];
        if (instruction.opcode == sci_vm::opcode::send_self && instruction.operands[0] == 8) {
            sends_callbacks = true;
            require(i > 0,"parser callback has no incoming accumulator");
            const auto& incoming = pipeline_script.instructions[i-1];
            require(incoming.opcode == sci_vm::opcode::load_immediate && incoming.operands.size() == 1,
                    "parser callback lost its native incoming accumulator");
            if (incoming.operands[0] == 0) ++word_callbacks;
            else if (incoming.operands[0] == 1) ++syntax_callbacks;
        }
        if (instruction.opcode == sci_vm::opcode::send && instruction.operands[0] == 6) sends_claimed = true;
    }
    require(sends_callbacks && sends_claimed,"portable parser bridges lost callback/property send frames");
    require(word_callbacks == 1 && syntax_callbacks == 2,"parser failure callbacks lost native accumulator values");
    bool has_lookup_index = false;
    for (const auto& section : pipeline_script.sections) if (section.type == 10) {
        const auto table = section.start+4+bridge_lookup.table*2;
        require(script_detail::word(pipeline_bytes,table,section.end) == 12 &&
                script_detail::word(pipeline_bytes,table+8,section.end) == 12,
                "Said lookup prefix index changed source byte order");
        has_lookup_index = true;
    }
    require(has_lookup_index,"Said lookup records were not emitted as script locals");
    parser_detail::ScriptBuilder empty_page_builder;
    const auto empty_page_lookup = parser_detail::emit_said_lookup(empty_page_builder,{});
    empty_page_builder.export_procedure(empty_page_lookup.procedure);
    const auto page_overhead = empty_page_builder.serialized_size()-2;
    const auto specification_cost = [](const Bytes& bytes) {
        return 2*(4+bytes.size()+1+parser_detail::said_node_count(parser_detail::compile_said(bytes,0,bytes.size()))*6);
    };
    const auto page_limit = page_overhead+std::max(specification_cost(said_fixture),specification_cost(reference_said));
    const auto lookup_pages = parser_detail::write_said_pages({said_fixture,reference_said,said_fixture},page_limit);
    require(lookup_pages.size() == 2 && lookup_pages[0].specifications == 1 && lookup_pages[1].specifications == 1,
            "Said lookup paging lost deduplication or capacity partitioning");
    for (const auto& page : lookup_pages)
        require(page.payload.size() <= page_limit && script_detail::parse(page.payload).exports.size() == 1,
                "Said lookup page exceeds its limit or lacks export zero");
    fails([&] { parser_detail::write_said_pages({said_fixture},page_overhead); },"capacity");
    parser_detail::ScriptBuilder dispatcher_builder;
    dispatcher_builder.export_procedure(parser_detail::emit_said_page_dispatch(dispatcher_builder,{2047,2046}));
    const auto dispatcher_script = script_detail::parse(dispatcher_builder.finish());
    unsigned external_pages = 0;
    for (const auto& instruction : dispatcher_script.instructions) if (instruction.opcode == sci_vm::opcode::call_external) {
        require(instruction.width == 2 && instruction.operands[1] == 0 && instruction.operands[2] == 4,
                "Said page dispatch narrowed script IDs or changed argument frames");
        ++external_pages;
    }
    require(external_pages == 2,"Said dispatcher omitted a lookup page");
    Game parser_source;
    parser_source.resources.push_back(resource(ResourceType::vocab,0,
        write_sci0_vocabulary({{"look",0x800,12},{"coin",0x100,21}})));
    Bytes parser_source_grammar;
    for (const auto& record : nested_grammar.records)
        for (const auto value : record) put(parser_source_grammar,value,2);
    parser_source.resources.push_back(resource(ResourceType::vocab,900,parser_source_grammar));
    Bytes source_selectors; put(source_selectors,4,2); source_selectors.resize(12);
    unsigned selector_slot = 0;
    for (const std::string name : {"claimed","wordFail","syntaxFail","number","elements"}) {
        const auto offset = source_selectors.size();
        source_selectors[2+2*selector_slot] = static_cast<std::uint8_t>(offset);
        source_selectors[3+2*selector_slot++] = static_cast<std::uint8_t>(offset >> 8);
        put(source_selectors,name.size(),2); source_selectors.insert(source_selectors.end(),name.begin(),name.end());
    }
    parser_source.resources.push_back(resource(ResourceType::vocab,997,source_selectors));
    Bytes parser_source_script{2,0,14,0,0x38,0xfe,7,0x46,0xff,7,0,0,0,0x48};
    put(parser_source_script,4,2); put(parser_source_script,4+said_fixture.size()+1,2);
    parser_source_script.insert(parser_source_script.end(),said_fixture.begin(),said_fixture.end()); parser_source_script.push_back(0);
    put(parser_source_script,3,2); put(parser_source_script,12,2);
    parser_source_script.insert(parser_source_script.end(),{1,0,2,0,2,0,3,0});
    put(parser_source_script,3,2); put(parser_source_script,8,2);
    parser_source_script.insert(parser_source_script.end(),{3,0,4,0}); // Later block is inactive.
    put(parser_source_script,7,2); put(parser_source_script,8,2); put(parser_source_script,1,2); put(parser_source_script,4,2); put(parser_source_script,0,2);
    parser_source.resources.push_back(resource(ResourceType::script,70,parser_source_script));
    parser_source.resources.push_back(resource(ResourceType::script,70,{0})); // First map entry remains authoritative.
    const auto parser_input = parser_detail::collect_parser_input(parser_source);
    require(parser_input.specifications == std::vector<Bytes>{said_fixture} && parser_input.synonyms.at(70).size() == 2 &&
            parser_input.selectors.claimed == 0 && parser_input.selectors.word_fail == 1 && parser_input.selectors.syntax_fail == 2,
            "Parser input collection lost source data or selector ordering");
    const auto parser_ids = parser_detail::allocate_parser_scripts(parser_input,2);
    require(parser_ids == std::vector<std::uint16_t>({2045,2044}),
            "Parser script allocation shadowed a source literal or guarded external script reference");
    const auto parser_runtime = parser_detail::write_parser_runtime(parser_input);
    require(parser_runtime.entry_script == parser_ids.front() && parser_runtime.scripts.size() == 2,
            "Parser runtime did not allocate an entry script and Said page");
    const auto runtime_entry = script_detail::parse(parser_runtime.scripts.front().payload);
    require(runtime_entry.exports.size() == 4,"Parser runtime export ABI changed");
    bool calls_lookup_page = false, disposes_synonym_list = false;
    for (const auto& instruction : runtime_entry.instructions) {
        if (instruction.opcode == sci_vm::opcode::call_external)
            calls_lookup_page |= instruction.operands.at(0) == parser_runtime.scripts.back().number;
        if (instruction.opcode == sci_vm::opcode::call_kernel)
            disposes_synonym_list |= instruction.operands.at(0) == sci_vm::sci1_kernel::dispose_list;
    }
    require(calls_lookup_page && disposes_synonym_list,
            "Parser runtime omitted Said dispatch or synonym list disposal");
    const auto selected_runtime = parser_detail::write_parser_runtime(parser_input,parser_detail::SynonymSelectors{4,3,1234});
    require(script_detail::parse(selected_runtime.scripts.front().payload).exports.size() == 5,
            "Portable parser runtime omitted its SetSynonyms export");
    auto vga_source = sample();
    vga_source.resources[1].payload = source_selectors;
    vga_source.resources[2].payload = {2,0,14,0,0x43,0x24,4,0x43,0x25,2,0x43,0x26,2,0x48,0,0};
    vga_source.resources.push_back(resource(ResourceType::vocab,0,
        write_sci0_vocabulary({{"look",0x800,12},{"coin",0x100,21}})));
    vga_source.resources.push_back(resource(ResourceType::vocab,900,parser_source_grammar));
    View vga_source_view; vga_source_view.loops.resize(1);
    ViewCel vga_source_cel; vga_source_cel.width = 1; vga_source_cel.height = 1; vga_source_cel.pixels = {1};
    vga_source_view.loops.front().cels.push_back(vga_source_cel);
    vga_source.resources.push_back(resource(ResourceType::view,0,write_sci0_view(vga_source_view)));
    vga_source.resources.push_back(resource(ResourceType::pic,200,{0xf0,4,0xf8,0,0,0,0xff}));
    const auto vga_candidate = migrate_sci0_to_sci1(vga_source,Sci1Graphics::vga);
    const auto vga_game = read_sci1_game(vga_candidate.files,Sci1Format::sci1_vga);
    const auto vga_resources = index_resources(vga_game);
    require(vga_resources.at({ResourceType::view,0})->payload.at(1) == 0x80 &&
            vga_resources.at({ResourceType::pic,200})->payload.at(0) == 0xfe,
            "VGA migration retained EGA picture/view resource formats");
    const auto vga_main = script_detail::parse(vga_resources.at({ResourceType::script,0})->payload);
    require(vga_main.exports.size() == 10 && vga_main.objects.front().methods.size() == 2 &&
            vga_main.strings.at(vga_main.objects.front().values.at(3)) == "\"scimate-vga\"",
            "VGA migration omitted runtime bridges or retained source game identity");
    const auto vga_calls = script_detail::parse(vga_resources.at({ResourceType::script,20})->payload);
    require(vga_calls.instructions.at(0).opcode == sci_vm::opcode::call_base &&
            vga_calls.instructions.at(0).operands.at(0) == 5 && vga_calls.instructions.at(1).operands.at(0) == 6 &&
            vga_calls.instructions.at(2).operands.at(0) == 7,
            "VGA migration did not redirect source Parse/Said/SetSynonyms calls");
    require(vga_candidate.report.find("SCI1 VGA early") != std::string::npos &&
            vga_candidate.report.find("UNVERIFIED") != std::string::npos,
            "VGA migration report misidentified the target or claimed runtime proof");
    bool has_pronoun_state = false;
    for (const auto& section : pipeline_script.sections) if (section.type == 10) {
        const auto reference = section.start+4+pipeline.pronouns.reference*2;
        has_pronoun_state = script_detail::word(pipeline_bytes,reference,section.end) == 0x1000;
    }
    require(has_pronoun_state,"portable pronoun state did not start with the undefined-reference marker");
    bool has_empty_synonyms = false;
    for (const auto& section : pipeline_script.sections) if (section.type == 10)
        has_empty_synonyms = script_detail::word(pipeline_bytes,section.start+4+pipeline.synonyms.blocks*2,section.end) == 0;
    require(has_empty_synonyms,"Active synonyms did not start empty");
    Bytes grammar_fixture;
    for (const auto value : {0x555,0x141,0x123,0,0,0,0,0,0,0,
                            0x123,0x146,0x800,0,0,0,0,0,0,0}) put(grammar_fixture,value,2);
    const auto grammar_table = parser_detail::read_grammar_table(grammar_fixture);
    require(grammar_table.root_id() == 0x555 && grammar_table.start_id() == 0x123,
            "portable parser assumed fixed grammar identities");
    auto invalid_grammar = grammar_fixture; invalid_grammar[4] = 0x24;
    fails([&] { parser_detail::read_grammar_table(invalid_grammar); },"undefined grammar start");
    const Bytes suffix_fixture{'*','i','e','s',0,1,0,'*','y',0,1,0,'*',0,255,255};
    const auto suffix_table = parser_detail::read_suffix_table(suffix_fixture);
    require(suffix_table.size() == 1 && suffix_table[0].alternative == "ies" && suffix_table[0].replacement == "y" &&
            suffix_table[0].result_class == 0x100 && suffix_table[0].class_mask == 0x100,
            "portable parser suffix strings or big-endian classes changed");
    fails([&] { parser_detail::read_suffix_table(Bytes{'*','s'}); },"truncated");
    parser_detail::ScriptBuilder lexicon_builder;
    const auto lexicon = parser_detail::emit_lexicon(lexicon_builder,
        {{"look",0x800,12},{"coin",0x100,21},{"coin",0x100,22}});
    lexicon_builder.export_procedure(lexicon.lookup);
    lexicon_builder.export_procedure(lexicon.word_class);
    lexicon_builder.export_procedure(lexicon.group);
    const auto lexicon_script = lexicon_builder.finish();
    const auto lexicon_metadata = script_detail::parse(lexicon_script);
    parser_detail::ScriptBuilder scanner_builder;
    const auto scanner_tables = parser_detail::emit_parser_tables(scanner_builder,grammar_table,suffix_table);
    const auto scanner_words = parser_detail::emit_lexicon(scanner_builder,
        {{"look",0x800,12},{"coin",0x100,22},{"enemy",0x100,23}});
    const auto word_resolver = parser_detail::emit_word_resolver(scanner_builder,scanner_words,scanner_tables);
    const auto scanner = parser_detail::emit_token_scanner(scanner_builder,word_resolver);
    scanner_builder.export_procedure(word_resolver.procedure); scanner_builder.export_procedure(scanner.procedure);
    const auto generated_scanner = script_detail::parse(scanner_builder.finish());
    require(generated_scanner.exports.size() == 2 && scanner.classes+64 == scanner.groups,
            "portable scanner token buffers or exports are invalid");
    std::set<unsigned> lexical_kernels;
    for (const auto& instruction : generated_scanner.instructions)
        if (instruction.opcode == sci_vm::opcode::call_kernel) lexical_kernels.insert(instruction.operands.at(0));
    require(lexical_kernels == std::set<unsigned>({0x45,0x46,0x47,0x62}),
            "portable word scanner depends on unexpected kernels");
    const auto character_table = parser_detail::scanner_character_table();
    require(character_table['A'] == ('a' | parser_detail::token_character) &&
            character_table[' '] == (' ' | parser_detail::token_boundary) &&
            character_table['-'] == ('-' | parser_detail::token_hyphen) &&
            character_table[','] == ',' && character_table[0x8e] == (0x84 | parser_detail::token_character),
            "portable scanner case folding or delimiter classification differs from SCI0");
    parser_detail::ScriptBuilder sentence_builder;
    const auto sentence_grammar = parser_detail::emit_compiled_grammar(sentence_builder,normalized_grammar);
    const auto sentence_tables = parser_detail::emit_parser_tables(sentence_builder,nested_grammar,suffix_table);
    const auto sentence_words = parser_detail::emit_lexicon(sentence_builder,{{"look",0x800,12},{"coin",0x100,21}});
    const auto sentence_resolver = parser_detail::emit_word_resolver(sentence_builder,sentence_words,sentence_tables);
    const auto sentence_tokens = parser_detail::emit_token_scanner(sentence_builder,sentence_resolver);
    const auto sentence_rules = parser_detail::emit_rule_selector(sentence_builder,sentence_grammar);
    const auto sentence_candidates = parser_detail::emit_candidate_procedures(sentence_builder,sentence_grammar);
    const auto sentence_parser = parser_detail::emit_sentence_parser(sentence_builder,sentence_tokens,sentence_grammar,
        sentence_rules,sentence_candidates);
    sentence_builder.export_procedure(sentence_tokens.procedure); sentence_builder.export_procedure(sentence_parser.procedure);
    const auto sentence_script = script_detail::parse(sentence_builder.finish());
    bool has_memory = false, has_lists = false;
    for (const auto& instruction : sentence_script.instructions) if (instruction.opcode == sci_vm::opcode::call_kernel) {
        require(instruction.operands[0] != 0x24 && instruction.operands[0] != 0x25,
                "portable sentence parser calls native Parse/Said");
        if (instruction.operands[0] == 0x72) has_memory = true;
        if (instruction.operands[0] == 0x2e) has_lists = true;
    }
    require(sentence_script.exports.size() == 2 && has_memory && has_lists,
            "portable sentence parser lacks candidate storage or worklists");
    require(lexicon.count == 2 && lexicon_metadata.exports.size() == 3,"portable parser lexicon exports/deduplication failed");
    bool lexicon_locals = false, lexicon_relocations = false;
    for (const auto& section : lexicon_metadata.sections) {
        if (section.type == 10) {
            const auto start = section.start+4;
            require(script_detail::word(lexicon_script,start+4,section.end) == 22 &&
                    script_detail::word(lexicon_script,start+10,section.end) == 12,
                    "portable parser vocabulary did not preserve final SCI0 meanings");
            lexicon_locals = true;
        }
        if (section.type == 8) lexicon_relocations = script_detail::word(lexicon_script,section.start+4,section.end) == 2;
    }
    require(lexicon_locals && lexicon_relocations,"portable parser string locals lack relocation records");
    parser_detail::ScriptBuilder unresolved_builder;
    const auto unresolved = unresolved_builder.label();
    unresolved_builder.export_procedure(unresolved); unresolved_builder.emit(sci_vm::opcode::return_);
    fails([&] { unresolved_builder.finish(); },"unbound");
    // Independent VGA cel fixture: literal, repeated colour and transparency.
    const Bytes view_fixture{1,0x80,0,0,1,0,0,0,10,0,1,0,0,0,16,0,
        8,0,1,0,0xff,2,255,0,2,17,18,0x83,42,0xc3};
    const auto fixture_view = read_sci1_vga_view(view_fixture);
    require(fixture_view.loops[0].cels[0].pixels == Bytes({17,18,42,42,42,255,255,255}) &&
            fixture_view.loops[0].cels[0].x == -1,"SCI1 VGA mixed RLE decoding failed");
    auto mirrored_view = fixture_view;
    ViewLoop mirrored_loop; mirrored_loop.mirror_of = 0;
    mirrored_view.loops.push_back(mirrored_loop);
    const auto encoded_view = write_sci1_vga_view(mirrored_view);
    require(encoded_view[1] == 0x80 && read_sci1_vga_view(encoded_view).loops == mirrored_view.loops,
            "SCI1 VGA mirror or palette-index encoding failed");
    auto truncated_view = view_fixture; truncated_view.pop_back();
    fails([&] { read_sci1_vga_view(truncated_view); },"truncated");
    Picture bitmap_picture;
    bitmap_picture.planes = {Bytes(320*190,1),Bytes(320*190,0),Bytes(320*190,0)};
    bitmap_picture.planes[1].back() = 15; bitmap_picture.planes[2].front() = 4;
    const auto vga_picture = write_sci1_vga_picture(bitmap_picture,ega_picture_palette());
    require(vga_picture[0] == 0xfe && vga_picture[1] == 2 && vga_picture[1286] == 0xfe && vga_picture[1287] == 1,
            "SCI1 VGA palette/bitmap commands are missing");
    require(vga_picture[1293] == 0x40 && vga_picture[1294] == 1 && vga_picture[1295] == 190 &&
            vga_picture[1299] == 255 && vga_picture[1301] == 0xbf && vga_picture[1302] == 1,
            "SCI1 VGA cel header/run encoding is invalid");
    require(vga_picture.back() == 0xff,"SCI1 VGA picture terminator missing");
    bitmap_picture.planes[0][0] = 255;
    fails([&] { write_sci1_vga_picture(bitmap_picture,ega_picture_palette()); },"transparency");
    auto volume_query = sample();
    volume_query.resources.push_back(resource(ResourceType::script,70,
        {7,0,8,0,1,0,12,0,2,0,15,0,0x3f,2,0x7a,0x39,8,0x8d,1,0x43,0x31,4,0x48,0,0}));
    const auto query_result = migrate_sci0_to_sci1(volume_query);
    const auto query_game = read_sci1_game(query_result.files,Sci1Format::sci1_late);
    bool query_found = false;
    for (const auto& item : query_game.resources) if (item.entry.id.type == ResourceType::script && item.entry.id.number == 70) {
        require(item.payload[14] == 0x78 && item.payload[17] == 0x33 && item.payload[18] == 0 &&
                item.payload[20] == 0x2d && item.payload[21] == 2,"uninitialized master-volume argument was not repaired");
        query_found = true;
    }
    require(query_found && query_result.report.find("Repaired invalid master-volume setter") != std::string::npos,
            "volume-query repair was not reported");
    volume_query.resources.back().payload = {7,0,8,0,1,0,12,0,2,0,19,0,0x3f,2,0x35,5,0xa5,1,
        0x7a,0x39,8,0x8d,1,0x43,0x31,4,0x48,0,0};
    require(migrate_sci0_to_sci1(volume_query).report.find("Repaired invalid master-volume setter") == std::string::npos,
            "initialized volume setter was changed");
    auto references = sample();
    auto& referenced_script = references.resources[2].payload;
    referenced_script.resize(referenced_script.size()-2);
    const Bytes filename{'D','A','T','A','.','B','I','N',0};
    put(referenced_script,5,2); put(referenced_script,filename.size()+4,2);
    referenced_script.insert(referenced_script.end(),filename.begin(),filename.end()); put(referenced_script,0,2);
    require(migrate_sci0_to_sci1(references).auxiliary_file_references == std::vector<std::string>{"DATA.BIN"},
            "auxiliary file reference was not exported");
    // Accumulator-only unary computation preserves an already-pushed sound
    // argument count and suboperation. Golden opcodes keep fixtures independent.
    for (const std::uint8_t unary : {std::uint8_t(0x00),std::uint8_t(0x16),std::uint8_t(0x18)}) {
        auto computed_sound = sample();
        computed_sound.resources[2].payload = {2,0,13,0,0x7a,0x39,4,unary,0x36,0x43,0x31,4,0x48,0,0};
        const auto converted = read_sci1_game(migrate_sci0_to_sci1(computed_sound).files,Sci1Format::sci1_late);
        require(converted.resources[1].payload[7] == unary && converted.resources[1].payload[10] == 0x2d,
                "computed sound argument changed or kernel was not remapped");
        // Arithmetic consumes a stack word: treating it like a unary operation
        // would mistake the consumed suboperation for a preserved frame.
        computed_sound.resources[2].payload[7] = 0x02; // add
        fails([&] { migrate_sci0_to_sci1(computed_sound); },"script 20: SCI0 DoSound frame");
    }
    // A missing optional script has an explicit, already implemented fallback.
    // Resource IDs and guard global are invented; no title-specific rule.
    auto optional_game = sample();
    const Bytes optional_code{2,0,24,0,0x81,7,0x31,9,0x76,0x46,0xbc,2,0,0,0,
                              0x33,6,0x39,97,0x76,0x5c,0x4a,4,0x48,0,0};
    optional_game.resources.push_back(resource(ResourceType::script,70,optional_code));
    const auto optional_result = migrate_sci0_to_sci1(optional_game);
    const auto optional_output = read_sci1_game(optional_result.files,Sci1Format::sci1_late);
    bool optional_found = false;
    for (const auto& item : optional_output.resources) if (item.entry.id.type == ResourceType::script && item.entry.id.number == 70) {
        auto expected = optional_code; expected[4] = 0x35; expected[5] = 0;
        require(item.payload == expected,"optional fallback changed bytes beyond its guard");
        optional_found = true;
    }
    require(optional_found && optional_result.report.find("missing optional script 700") != std::string::npos,
            "optional fallback was lost or not reported");
    auto unsafe_optional = optional_game;
    unsafe_optional.resources.back().payload[21] = 0x39; // No fallback operation.
    fails([&] { migrate_sci0_to_sci1(unsafe_optional); },"without a verified fallback");
    unsafe_optional = optional_game;
    auto& exposed_arm = unsafe_optional.resources.back().payload;
    exposed_arm.resize(exposed_arm.size()-2);
    exposed_arm.insert(exposed_arm.end(),{7,0,8,0,1,0,9,0,0,0});
    fails([&] { migrate_sci0_to_sci1(unsafe_optional); },"without a verified fallback");
    unsafe_optional = optional_game;
    unsafe_optional.resources.push_back(resource(ResourceType::script,700,{0,0}));
    fails([&] { migrate_sci0_to_sci1(unsafe_optional); },"has no export slot");
    unsafe_optional = sample();
    unsafe_optional.resources.push_back(resource(ResourceType::script,70,{2,0,11,0,0x46,0xbc,2,0,0,0,0x48,0,0}));
    fails([&] { migrate_sci0_to_sci1(unsafe_optional); },"without a verified fallback");
    // Uncalled legacy export slots are metadata, not mandatory procedures.
    auto legacy_exports = sample();
    legacy_exports.resources.push_back(resource(ResourceType::script,41,{7,0,8,0,1,0,0xff,0xff,0,0}));
    const auto legacy_result = migrate_sci0_to_sci1(legacy_exports);
    const auto legacy_output = read_sci1_game(legacy_result.files,Sci1Format::sci1_late);
    bool legacy_found = false;
    for (const auto& item : legacy_output.resources) if (item.entry.id.type == ResourceType::script && item.entry.id.number == 41) {
        require(item.payload == legacy_exports.resources.back().payload,"opaque export slot changed"); legacy_found = true;
    }
    require(legacy_found && legacy_result.report.find("preserved: 1") != std::string::npos,"legacy export preservation not reported");
    for (const unsigned opaque : {65535u,25u}) {
        auto main_exports = sample();
        auto& bytes = main_exports.resources[0].payload;
        bytes.insert(bytes.begin()+8,2,0);
        patch(bytes,2,10); patch(bytes,4,2); patch(bytes,6,42); patch(bytes,8,opaque);
        patch(bytes,48,25); patch(bytes,60,48);
        const auto converted = read_sci1_game(migrate_sci0_to_sci1(main_exports).files,Sci1Format::sci1_late);
        const auto decoded = scimate::script_detail::parse(converted.resources[0].payload,true,false);
        require(decoded.exports.size() == 6 && decoded.exports[1].second == opaque,
                "main export expansion corrupted an opaque export");
    }
    fails([&] { scimate::script_detail::parse(legacy_exports.resources.back().payload); },"export target");
    // A direct CALLE to that same invalid slot is rejected with source context.
    auto called_legacy = legacy_exports;
    called_legacy.resources.push_back(resource(ResourceType::script,42,{2,0,9,0,0x47,41,0,0,0x48,0,0}));
    fails([&] { migrate_sci0_to_sci1(called_legacy); },"script 42 at byte 4");
    // Valid code exports remain callable and are retained unchanged.
    called_legacy.resources[3].payload = {7,0,8,0,1,0,12,0,2,0,5,0,0x48,0,0};
    require(!migrate_sci0_to_sci1(called_legacy).files.map.empty(),"valid CALLE export rejected");
    // Legacy non-main branches survive only with their original layout intact.
    auto bad_branch = sample();
    bad_branch.resources.push_back(resource(ResourceType::script,43,{2,0,7,0,0x33,1,0x48,0,0}));
    bad_branch.resources.push_back(resource(ResourceType::script,45,{2,0,9,0,0x30,0xff,0x7f,0x3a,0x48,0,0}));
    const auto branch_result = migrate_sci0_to_sci1(bad_branch);
    const auto branch_output = read_sci1_game(branch_result.files,Sci1Format::sci1_late);
    bool branch_found = false;
    for (const auto& item : branch_output.resources) if (item.entry.id.type == ResourceType::script && item.entry.id.number == 43) {
        require(item.payload == bad_branch.resources[3].payload,"legacy branch bytes changed"); branch_found = true;
    }
    require(branch_found && branch_result.report.find("script 43 at byte 4 -> 7") != std::string::npos,
            "legacy branch lost or omitted from report");
    require(branch_result.report.find("script 45 at byte 4 -> 32774") != std::string::npos,
            "out-of-range wide conditional branch omitted from report");
    fails([&] { scimate::script_detail::parse(bad_branch.resources.back().payload); },"branch/call target");
    auto bad_main_branch = sample();
    bad_main_branch.resources[0].payload[12] = 0x33;
    bad_main_branch.resources[0].payload[13] = 127;
    fails([&] { migrate_sci0_to_sci1(bad_main_branch); },"script 0");
    auto bad_local_call = sample();
    bad_local_call.resources.push_back(resource(ResourceType::script,44,{2,0,8,0,0x41,127,0,0x48,0,0}));
    fails([&] { migrate_sci0_to_sci1(bad_local_call); },"local call at byte 4");
    auto parser_game = sample();
    parser_game.resources[2].payload = {2,0,14,0,0x43,0x24,4,0x43,0x25,2,0x43,0x26,2,0x48,0,0};
    parser_game.resources.push_back(resource(ResourceType::vocab,0,write_sci0_vocabulary({{"look",0x80,0x123},{"tree",0x10,0x456}})));
    Bytes grammar(20,0); grammar[0] = 1;
    parser_game.resources.push_back(resource(ResourceType::vocab,900,grammar));
    const Bytes suffix{0,0xff,0xff};
    parser_game.resources.push_back(resource(ResourceType::vocab,901,suffix));
    parser_game.resources.push_back(resource(ResourceType::vocab,999,{1,2,3}));
    View view; view.loops.resize(1);
    ViewCel cel; cel.width = 2; cel.height = 10; cel.pixels.assign(20,1);
    view.loops[0].cels.push_back(cel);
    parser_game.resources.push_back(resource(ResourceType::view,0,write_sci0_view(view)));
    const auto parser_result = migrate_sci0_to_sci1(parser_game);
    const auto parser_output = read_sci1_game(parser_result.files,Sci1Format::sci1_ega);
    bool words_found = false, grammar_found = false, suffix_found = false;
    for (const auto& item : parser_output.resources) if (item.entry.id.type == ResourceType::vocab) {
        require(item.entry.id.number != 0 && item.entry.id.number != 999,"obsolete parser/kernel vocabulary retained");
        if (item.entry.id.number == 900) {
            const Bytes records{0,'l','o','o','k',0,8,1,0x23,0,'t','r','e','e',0,1,4,0x56};
            require(Bytes(item.payload.begin()+510,item.payload.end()) == records,"SCI1 word spelling/class/group changed");
            require(scimate::script_detail::word(item.payload,2*unsigned('l'),510) == 510,"word index offset incorrect");
            words_found = true;
        }
        if (item.entry.id.number == 901) { require(item.payload == grammar,"grammar changed during relocation"); grammar_found = true; }
        if (item.entry.id.number == 902) { require(item.payload == suffix,"suffix rules changed during relocation"); suffix_found = true; }
    }
    require(words_found && grammar_found && suffix_found,"parser resource relocation incomplete");
    require(parser_result.report.find("SCI1 EGA") != std::string::npos,"parser target was not reported");
    auto broken_ega = parser_result.files; broken_ega.volumes[0].pop_back();
    fails([&] { read_sci1_game(broken_ega,Sci1Format::sci1_ega); },"truncated");
    fails([] { write_sci1_game({resource(ResourceType::script,2048,{})},Sci1Format::sci1_ega); },"packed limit");
    fails([] { write_sci1_game({resource(ResourceType::heap,0,{})},Sci1Format::sci1_ega); },"packed limit");
    auto bad_parser = parser_game;
    bad_parser.resources.push_back(resource(ResourceType::vocab,902,{0}));
    fails([&] { migrate_sci0_to_sci1(bad_parser); },"conflicts");
    bad_parser = parser_game; bad_parser.resources[4].payload.pop_back();
    fails([&] { migrate_sci0_to_sci1(bad_parser); },"20-byte");
    bad_parser = parser_game; bad_parser.resources[2].payload[6] = 2;
    fails([&] { migrate_sci0_to_sci1(bad_parser); },"argument frame");
    // SCI0 sound ABI is retained only after proving both call frames and the
    // resource layout; selector names alone do not establish compatibility.
    constexpr unsigned sound_args[]{2,2,1,2,1,2,2,1,1,2,2,1,1};
    for (unsigned subop = 0; subop <= 12; ++subop) {
        auto sound_game = sample();
        Bytes code{0x39,static_cast<std::uint8_t>(sound_args[subop]),0x39,static_cast<std::uint8_t>(subop)};
        if (sound_args[subop] == 2)
            code.push_back(subop == 4 || subop == 6 || subop == 8 ? 0x76 : 0x7c); // integer / object
        code.insert(code.end(),{0x43,0x31,static_cast<std::uint8_t>(2*sound_args[subop]),0x48});
        Bytes script; put(script,2,2); put(script,code.size()+4,2);
        script.insert(script.end(),code.begin(),code.end()); put(script,0,2);
        sound_game.resources[2].payload = script;
        Bytes sound(33,0); sound.insert(sound.end(),{0,0x90,60,100,10,0x80,60,0,0,0xfc});
        sound_game.resources.push_back(resource(ResourceType::sound,8,sound));
        const auto result = migrate_sci0_to_sci1(sound_game);
        const auto reopened = read_sci1_game(result.files,Sci1Format::sci1_late);
        bool found_sound = false, found_call = false;
        for (const auto& item : reopened.resources) {
            if (item.entry.id.type == ResourceType::sound) {
                require(item.payload == sound,"validated sound payload changed"); found_sound = true;
            }
            if (item.entry.id.type == ResourceType::script && item.entry.id.number == 20) {
                const auto decoded = scimate::script_detail::parse(item.payload);
                for (const auto& inst : decoded.instructions) if (inst.opcode == 33) {
                    require(inst.operands[0] == 0x2d,"DoSound kernel not remapped"); found_call = true;
                }
            }
        }
        require(found_sound && found_call,"sound migration lost resources/calls");
        auto invalid = sound_game;
        invalid.resources.back().payload.resize(10);
        fails([&] { migrate_sci0_to_sci1(invalid); },"sound 8");
        invalid = sound_game;
        invalid.resources[1].payload = {0,0,4,0,7,0,'n','o','d','e','P','t','r'};
        fails([&] { migrate_sci0_to_sci1(invalid); },"nodePtr");
        invalid = sound_game;
        invalid.resources[2].payload[7] = 13;
        fails([&] { migrate_sci0_to_sci1(invalid); },"suboperation");
        invalid = sound_game;
        invalid.resources[2].payload[6] = 0x36; // push accumulator: dynamic subop
        invalid.resources[2].payload.erase(invalid.resources[2].payload.begin()+7);
        patch(invalid.resources[2].payload,2,script.size()-3);
        fails([&] { migrate_sci0_to_sci1(invalid); },"suboperation");
    }
    for (auto format : {Sci1Format::sci1_late,Sci1Format::sci11}) {
        const std::vector<Resource> input{resource(ResourceType::heap,60000,{1,2,3}),resource(ResourceType::script,0,{0,0})};
        auto files = write_sci1_game(input,format);
        auto map = read_sci1_map(files.map,format);
        require(map.size() == 2 && map[0].id.type == ResourceType::script && map[1].id.type == ResourceType::heap, "SCI1 directory grouping failed");
        if (format == Sci1Format::sci11) require(map[1].offset%2 == 0,"SCI1.1 word offset is not aligned");
        auto reopened = read_sci1_game(files,format);
        require(reopened.resources[0].payload == input[1].payload && reopened.resources[1].payload == input[0].payload,"SCI1 payload mismatch");
        auto broken = files; broken.volumes[0].pop_back();
        fails([&] { read_sci1_game(broken,format); }, "truncated resource payload");
        broken = files; broken.volumes[0][1] = 7;
        fails([&] { read_sci1_game(broken,format); }, "ID differs");
        broken = files; broken.map[2] = 255;
        fails([&] { read_sci1_map(broken.map,format); }, "first resource table");
        broken = files; broken.map[3] = broken.map[0];
        fails([&] { read_sci1_map(broken.map,format); }, "duplicate");
        fails([&] { read_sci1_map(Bytes(files.map.begin(),files.map.begin()+2),format); },"truncated");
        require(read_sci1_game(write_sci1_game({},format),format).resources.empty(),"empty SCI1 game failed");
    }
    require(decode_sci1(1,huffman,2) == Bytes({'A','B'}),"SCI1 Huffman method assignment failed");
    require(decode_sci1(2,msb({{256,9},{65,9},{258,9},{257,9}}),3) == Bytes({'A','A','A'}),"SCI1 overlapping LZW reference failed");
    std::vector<std::pair<unsigned,unsigned>> codes{{256,9}};
    Bytes expected;
    // A known literal stream crosses SCI1's early-change width boundary:
    // 254 literals use nine bits, the following 46 use ten bits.
    for (unsigned i = 0; i < 300; ++i) { codes.emplace_back(i%256,i<254?9:10); expected.push_back(static_cast<std::uint8_t>(i)); }
    codes.emplace_back(257,10);
    require(decode_sci1(2,msb(codes),expected.size()) == expected,"SCI1 LZW width boundary failed");
    fails([] { decode_sci1(2,msb({{256,9},{259,9},{257,9}}),1); },"dictionary");
    fails([] { decode_sci1(2,msb({{256,9},{65,9}}),1); },"truncated");
    fails([] { decode_sci1(18,{},0); },"unsupported SCI1 compression");
    fails([] { write_sci1_game({resource(ResourceType::view,0,Bytes(65532))},Sci1Format::sci1_late); },"header limit");
    auto migrated = migrate_sci0_to_sci1(sample());
    auto output = read_sci1_game(migrated.files,Sci1Format::sci1_late);
    const auto& main = output.resources[0].payload;
    const auto parsed = scimate::script_detail::parse(main);
    require(parsed.exports.size() == 5,"file-I/O bridges not exported");
    require(parsed.objects[0].pointer == 40 && parsed.objects[0].values[3] == 23,"main absolute addresses changed");
    require(main[16] == 0x29,"main kernel not remapped");
    const auto source_main = sample().resources.front().payload;
    const auto source_metadata = script_detail::parse(source_main);
    const auto renamed_main = migration_detail::rename_main_game_object(source_main,"SCIMATEVGA");
    const auto renamed_metadata = script_detail::parse(renamed_main);
    const auto renamed_pointer = renamed_metadata.objects.front().values.at(3);
    require(renamed_metadata.objects.front().pointer == source_metadata.objects.front().pointer &&
            renamed_metadata.exports.front().second == source_metadata.exports.front().second &&
            renamed_metadata.strings.at(renamed_pointer) == "\"SCIMATEVGA\"" &&
            renamed_metadata.strings.at(23) == "\"DEMO\"",
            "Migration identity changed original addresses or reused a shared string");
    auto missing_name_relocation = source_main;
    patch(missing_name_relocation,58,40); // Active relocation no longer names the name property.
    fails([&] { migration_detail::rename_main_game_object(missing_name_relocation,"SCIMATEVGA"); },"active relocation");
    fails([&] { migration_detail::rename_main_game_object(source_main,std::string("bad\0name",8)); },"ASCII");
    const auto forwarded_parser = migration_detail::append_script_exports(source_main,source_metadata,
        {migration_detail::forward_script_export(parser_runtime.entry_script,static_cast<unsigned>(parser_detail::ParserExport::parse))});
    const auto forwarded_metadata = script_detail::parse(forwarded_parser);
    const auto forwarded_start = forwarded_metadata.exports.back().second;
    require(forwarded_parser.at(forwarded_start) == sci_vm::encoded_opcode(sci_vm::opcode::push_zero) &&
            forwarded_parser.at(forwarded_start+1) == sci_vm::encoded_opcode(sci_vm::opcode::rest,true) &&
            forwarded_parser.at(forwarded_start+2) == 1,
            "Parser bridge does not forward the caller's actual arguments");
    bool forwards_parser_export = false;
    for (const auto& instruction : forwarded_metadata.instructions)
        if (instruction.start >= forwarded_start && instruction.opcode == sci_vm::opcode::call_external)
            forwards_parser_export = instruction.operands == std::vector<unsigned>{parser_runtime.entry_script,0,0};
    require(forwards_parser_export,"Parser bridge changed the runtime script, export or fixed frame");
    auto preserved_main = source_main;
    preserved_main[16] = 0x29; // Existing kernel remap is the only source-byte change.
    require(std::equal(preserved_main.begin(),preserved_main.begin()+source_metadata.sections.back().end,main.begin()),
            "Appending file bridges moved or modified source data");
    const auto with_extra_export = migration_detail::append_script_exports(main,parsed,{Bytes{0x48}});
    const auto extra_metadata = script_detail::parse(with_extra_export);
    require(extra_metadata.exports.size() == 6 && extra_metadata.exports[0].second == parsed.exports[0].second &&
            std::equal(main.begin(),main.begin()+parsed.sections.back().end,with_extra_export.begin()),
            "Appending another bridge changed the existing script or export ABI");
    const auto& other = output.resources[1].payload;
    require(other[4] == 0x45 && other[5] == 1 && other[8] == 2 && other[11] == 3 && other[14] == 4,"file-I/O calls not bridged");
    require(other[17] == 0x3c,"kernel numbering shift failed");
    require(migrated.report.find("UNVERIFIED") != std::string::npos,"migration claims unproven runtime success");
    auto unsupported = sample(); unsupported.resources[0].payload[23] = 'X';
    require(!migrate_sci0_to_sci1(unsupported).files.map.empty(),"migration depends on game identity");
    unsupported = sample(); unsupported.resources[2].payload[5] = 0x55;
    fails([&] { migrate_sci0_to_sci1(unsupported); },"DoAvoider");
    unsupported = sample(); unsupported.resources[2].payload[6] = 3;
    fails([&] { migrate_sci0_to_sci1(unsupported); },"odd SCI0 file-I/O");
    auto short_open = sample(); short_open.resources[2].payload[6] = 2;
    const auto forwarded = migrate_sci0_to_sci1(short_open);
    const auto forwarded_game = read_sci1_game(forwarded.files,Sci1Format::sci1_late);
    require(forwarded_game.resources[1].payload[6] == 2,"short FOpen frame was changed");
    require(forwarded.report.find("frames retained: 1") != std::string::npos,"unusual FOpen frame not reported");
    const auto bridged_main = scimate::script_detail::parse(forwarded_game.resources[0].payload);
    for (std::size_t i = 1; i < bridged_main.exports.size(); ++i) {
        auto start = bridged_main.exports[i].second;
        const auto& payload = forwarded_game.resources[0].payload;
        if (i == 4) {
            require(payload[start] == 0x3f && payload[start+1] == 1 && payload[start+2] == 0xa5 && payload[start+3] == 0,
                    "FClose bridge did not save the source accumulator");
            start += 4;
            require(payload[start+8] == 0x85 && payload[start+9] == 0 && payload[start+10] == 0x48,
                    "FClose bridge did not restore the source accumulator");
        }
        // Golden VM bytes: push1; pushi subop; rest 1; callk FileIO, 2; ret.
        require(payload[start] == 0x78 && payload[start+3] == 0x59 && payload[start+4] == 1 &&
                payload[start+5] == 0x43 && payload[start+6] == 0x74 && payload[start+7] == 2 && (i == 4 || payload[start+8] == 0x48),
                "FileIO bridge does not forward the runtime parameter list");
    }
    unsupported = sample(); unsupported.resources[2].payload[5] = 0x24;
    fails([&] { migrate_sci0_to_sci1(unsupported); },"parser migration");
    unsupported = sample(); unsupported.resources[2].payload[5] = 0x31;
    fails([&] { migrate_sci0_to_sci1(unsupported); },"DoSound");
    // Source SCI0 duplicate resolution matches ScummVM (first entry wins).
    auto duplicates = sample(); duplicates.resources.push_back(resource(ResourceType::script,20,{}));
    require(migrate_sci0_to_sci1(duplicates).files.map == migrated.files.map,"SCI0 duplicate resolution changed");
}
} // namespace
int main() {
    try { tests(); std::cout << "SCI1 format and migration tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
