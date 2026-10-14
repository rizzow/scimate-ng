// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_bridges.hpp"
#include "parser_said.hpp"
#include "parser_synonyms.hpp"
#include "scimate/resource_lookup.hpp"
#include "vocabulary_names.hpp"

namespace scimate::parser_detail {
namespace parser_resource {
inline constexpr unsigned words = 0, grammar = 900, suffixes = 901, selectors = 997, classes = 996;
inline constexpr unsigned maximum_script_id = sci_vm::maximum_packed_resource_id;
}
struct ParserInput {
    std::vector<Word> words;
    GrammarTable grammar;
    std::vector<SuffixRule> suffixes;
    ParserSelectors selectors;
    std::vector<Bytes> specifications;
    std::map<std::uint16_t,std::vector<SynonymPair>> synonyms;
    std::set<unsigned> reserved_scripts;
    unsigned global_count = 0;
};
inline unsigned parser_selector(const vocabulary_detail::Names& names, const char* requested) {
    for (unsigned i = 0; i < names.entries.size(); ++i) {
        const auto& name = names.entries[i];
        if (name && std::string(name->begin(),name->end()) == requested) return i;
    }
    throw std::runtime_error(std::string("portable parser: missing selector ")+requested);
}
inline ParserInput collect_parser_input(const Game& game) {
    const auto resources = index_resources(game);
    const auto required = [&](ResourceType type, unsigned number) -> const Bytes& {
        const auto found = resources.find({type,static_cast<std::uint16_t>(number)});
        if (found == resources.end()) throw std::runtime_error("portable parser: missing resource "+
            std::to_string(static_cast<unsigned>(type))+":"+std::to_string(number));
        return found->second->payload;
    };
    ParserInput input;
    for (const unsigned number : {1u,10u,910u,911u})
        if (resources.count({ResourceType::vocab,static_cast<std::uint16_t>(number)}))
            throw std::runtime_error("portable parser: translated/secondary vocabulary requires an explicit language profile");
    input.words = read_sci0_vocabulary(required(ResourceType::vocab,parser_resource::words));
    input.grammar = read_grammar_table(required(ResourceType::vocab,parser_resource::grammar));
    const auto suffixes = resources.find({ResourceType::vocab,parser_resource::suffixes});
    if (suffixes != resources.end()) input.suffixes = read_suffix_table(suffixes->second->payload);
    const auto selectors = vocabulary_detail::read_names(required(ResourceType::vocab,parser_resource::selectors),true);
    input.selectors = {parser_selector(selectors,"claimed"),parser_selector(selectors,"wordFail"),parser_selector(selectors,"syntaxFail")};
    const auto reserve = [&](unsigned value) { if (value <= parser_resource::maximum_script_id) input.reserved_scripts.insert(value); };
    std::set<Bytes> specifications;
    for (const auto& entry : resources) {
        reserve(entry.first.second);
        if (entry.first.first != ResourceType::script) continue;
        const auto& bytes = entry.second->payload;
        const auto script = script_detail::parse(bytes,false,false);
        for (const auto& instruction : script.instructions) {
            if (instruction.opcode == sci_vm::opcode::call_external) reserve(instruction.operands.at(0));
            if (instruction.opcode == sci_vm::opcode::load_immediate || instruction.opcode == sci_vm::opcode::push_immediate)
                reserve(instruction.operands.at(0));
        }
        for (const auto& object : script.objects) for (const auto value : object.values) reserve(value);
        bool globals_seen = false;
        for (const auto& section : script.sections) {
            if (entry.first.second == 0 && section.type == sci_vm::section::locals && !globals_seen) {
                input.global_count = static_cast<unsigned>((section.end-section.start-sci_vm::section_header_bytes)/sci_vm::word_bytes);
                globals_seen = true;
            }
            constexpr unsigned synonyms_section = 3, said_section = 4;
            const auto begin = section.start+sci_vm::section_header_bytes;
            if (section.type == synonyms_section && !input.synonyms.count(entry.first.second)) {
                const auto pairs = read_synonym_pairs(Bytes(bytes.begin()+begin,bytes.begin()+section.end));
                // The interpreter uses the first synonym block, including an
                // empty one; later blocks must not extend its aliases.
                input.synonyms.emplace(entry.first.second,pairs);
            }
            if (section.type != said_section) continue;
            auto cursor = begin;
            while (cursor < section.end) {
                if (section.end-cursor == 1 && bytes[cursor] == 0) break; // Word-alignment padding.
                const auto tokens = tokenize_said(bytes,cursor,section.end);
                SaidCompiler(tokens.tokens).compile();
                specifications.emplace(bytes.begin()+cursor,bytes.begin()+cursor+tokens.bytes_consumed);
                cursor += tokens.bytes_consumed;
            }
        }
    }
    const auto classes = resources.find({ResourceType::vocab,parser_resource::classes});
    if (classes != resources.end()) {
        const auto& bytes = classes->second->payload;
        constexpr unsigned class_record_bytes = 4, script_field_offset = 2;
        if (bytes.size()%class_record_bytes) invalid_parser_table("incomplete class-to-script record");
        for (std::size_t offset = 0; offset < bytes.size(); offset += class_record_bytes)
            reserve(table_word(bytes,offset+script_field_offset));
    }
    input.reserved_scripts.insert(0);
    input.specifications.assign(specifications.begin(),specifications.end());
    return input;
}
inline std::vector<std::uint16_t> allocate_parser_scripts(const ParserInput& input, std::size_t count) {
    std::vector<std::uint16_t> scripts;
    for (unsigned number = parser_resource::maximum_script_id; number && scripts.size() < count; --number)
        if (!input.reserved_scripts.count(number)) scripts.push_back(static_cast<std::uint16_t>(number));
    if (scripts.size() != count) throw std::runtime_error("portable parser: insufficient unreserved script IDs");
    return scripts;
}
} // namespace scimate::parser_detail
