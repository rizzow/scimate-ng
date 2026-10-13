// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/script.hpp"
#include <map>
#include <set>
namespace scimate::script_detail {
struct Section { std::size_t start, end; unsigned type; };
struct Instruction { std::size_t start, end; unsigned opcode, width; std::vector<unsigned> operands; };
struct Object { std::size_t pointer; bool is_class; std::vector<unsigned> values, selectors, methods, addresses; };
struct ParsedScript {
    std::vector<Section> sections;
    std::map<std::size_t,std::string> strings;
    std::vector<Instruction> instructions;
    std::vector<Object> objects;
    std::vector<std::pair<std::size_t,unsigned>> exports;
    std::set<std::size_t> boundaries, labels, procedures, object_pointers;
};
// Migration may preserve legacy inactive exports/dead branch targets. Layout and
// instruction operand bounds are always validated; normal inspection is strict.
ParsedScript parse(const Bytes&, bool validate_targets = true, bool validate_export_targets = true);
// Migration can preserve opaque export slots without weakening branch checks.
// Consumers executing exports must validate their target independently.
std::uint16_t word(const Bytes&,std::size_t,std::size_t);
int signed_value(unsigned,unsigned);
std::uint16_t target(const Instruction&);
std::string hex(std::size_t,int=4);
std::string opcode_name(unsigned);
std::vector<unsigned> operand_widths(unsigned opcode, unsigned width);
std::string quoted(const Bytes&,std::size_t,std::size_t);
}
