/***************************************************************************
    Copyright (c) 2015 Philip Fortier

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
***************************************************************************/
// Adapted opcode/layout knowledge from PMachine.cpp, CompiledScript.cpp,
// Disassembler.cpp. New bounded parser/formatter; see docs/provenance.md.
#include "script_internal.hpp"

#include <array>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace scimate {
namespace script_detail {
[[noreturn]] void fail(std::size_t offset, const char* message) {
    throw std::runtime_error("SCI0 script at byte " + std::to_string(offset) + ": " + message);
}
std::string hex(std::size_t value, int width) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(width) << value;
    return out.str();
}
std::uint16_t word(const Bytes& bytes, std::size_t offset, std::size_t end) {
    if (offset > end || end - offset < 2) fail(offset, "truncated word");
    return static_cast<std::uint16_t>(bytes[offset] | (static_cast<unsigned>(bytes[offset + 1]) << 8));
}
std::string quoted(const Bytes& bytes, std::size_t begin, std::size_t end) {
    std::string text = "\"";
    for (auto i = begin; i < end; ++i) {
        const auto c = bytes[i];
        if (c == '\\' || c == '"') { text += '\\'; text += static_cast<char>(c); }
        else if (c == '\n') text += "\\n";
        else if (c == '\r') text += "\\r";
        else if (c == '\t') text += "\\t";
        else if (c >= 32 && c < 127) text += static_cast<char>(c);
        else text += "\\x" + hex(c, 2);
    }
    return text + '"';
}
std::string opcode_name(unsigned op) {
    constexpr std::array<const char*, 64> names{
        "bnot", "add", "sub", "mul", "div", "mod", "shr", "shl", "xor", "and", "or", "neg",
        "not", "eq?", "ne?", "gt?", "ge?", "lt?", "le?", "ugt?", "uge?", "ult?", "ule?",
        "bt", "bnt", "jmp", "ldi", "push", "pushi", "toss", "dup", "link", "call", "callk",
        "callb", "calle", "ret", "send", nullptr, nullptr, "class", nullptr, "self", "super",
        "rest", "lea", "selfID", nullptr, "pprev", "pToa", "aTop", "pTos", "sTop", "ipToa",
        "dpToa", "ipTos", "dpTos", "lofsa", "lofss", "push0", "push1", "push2", "pushSelf", nullptr
    };
    if (op < 64) return names[op] ? names[op] : "";
    std::string name(1, "ls+-"[(op - 64) / 16]);
    name += (op & 4) ? 's' : 'a';
    name += "gltp"[op & 3];
    if (op & 8) name += 'i';
    return name;
}
std::vector<unsigned> operand_widths(unsigned op, unsigned width) {
    if (op == 35) return {width, width, 1};
    if (op == 32 || op == 33 || op == 34 || op == 43) return {width, 1};
    if (op == 37 || op == 42) return {1};
    if (op == 45) return {width, width};
    if ((op >= 23 && op <= 26) || op == 28 || op == 31 || op == 40 || op == 44 ||
        (op >= 49 && op <= 58) || op >= 64) return {width};
    return {};
}
int signed_value(unsigned value, unsigned width) {
    const unsigned top = width == 1 ? 128 : 32768;
    return value >= top ? static_cast<int>(value) - static_cast<int>(top * 2) : static_cast<int>(value);
}
std::uint16_t target(const Instruction& instruction) {
    // SCI addresses wrap at 16 bits. Base is AFTER the entire instruction,
    // including the fixed byte argument count on call.
    return static_cast<std::uint16_t>(static_cast<int>(instruction.end) +
                                     signed_value(instruction.operands[0], instruction.width));
}
const char* section_name(unsigned type) {
    constexpr std::array<const char*, 11> names{
        "end", "instance", "code", "synonyms", "said", "strings", "class", "exports",
        "relocations", "preload-text", "locals"
    };
    return type < names.size() ? names[type] : "unknown";
}
ParsedScript parse(const Bytes& bytes) {
    if (bytes.size() > 65535) fail(0, "payload exceeds SCI0 size limit");
    std::vector<Section> sections;
    std::size_t cursor = 0;
    for (;;) {
        const auto type = word(bytes, cursor, bytes.size());
        if (type == 0) {
            cursor += 2;
            for (auto i = cursor; i < bytes.size(); ++i)
                if (bytes[i] != 0) fail(i, "nonzero data after section terminator");
            break;
        }
        const auto size = word(bytes, cursor + 2, bytes.size());
        if (size < 4 || size > bytes.size() - cursor) fail(cursor, "invalid section size");
        sections.push_back({cursor, cursor + size, type});
        cursor += size;
    }
    std::map<std::size_t, std::string> strings;
    std::vector<Instruction> instructions;
    std::vector<Object> objects;
    std::vector<std::pair<std::size_t, unsigned>> exports;
    std::set<std::size_t> boundaries, labels, procedures;
    for (const auto& section : sections) {
        auto pos = section.start + 4;
        const auto end = section.end;
        if (section.type == 5) {
            while (pos < end) {
                const auto start = pos;
                while (pos < end && bytes[pos] != 0) ++pos;
                if (pos == end) fail(start, "unterminated string");
                strings[start] = quoted(bytes, start, pos++);
            }
        } else if (section.type == 2) {
            while (pos < end) {
                Instruction instruction{pos, 0, static_cast<unsigned>(bytes[pos] >> 1),
                                        (bytes[pos] & 1u) ? 1u : 2u, {}};
                if (opcode_name(instruction.opcode).empty()) fail(pos, "reserved SCI0 opcode");
                ++pos;
                for (auto width : operand_widths(instruction.opcode, instruction.width)) {
                    if (width > end - pos) fail(instruction.start, "truncated instruction operand");
                    instruction.operands.push_back(width == 1 ? bytes[pos] : word(bytes, pos, end));
                    pos += width;
                }
                instruction.end = pos;
                boundaries.insert(instruction.start);
                instructions.push_back(std::move(instruction));
            }
        } else if (section.type == 1 || section.type == 6) {
            if (word(bytes, pos, end) != 0x1234) fail(pos, "unsupported object header (expected 0x1234)");
            const auto count = word(bytes, pos + 6, end);
            if (count < 3) fail(pos + 6, "object requires at least three properties");
            Object object{pos + 8, section.type == 6, {}, {}, {}, {}};
            pos += 8;
            for (unsigned i = 0; i < count; ++i, pos += 2) object.values.push_back(word(bytes, pos, end));
            if (object.is_class)
                for (unsigned i = 0; i < count; ++i, pos += 2) object.selectors.push_back(word(bytes, pos, end));
            const auto methods = word(bytes, pos, end); pos += 2;
            for (unsigned i = 0; i < methods; ++i, pos += 2) object.methods.push_back(word(bytes, pos, end));
            if (word(bytes, pos, end) != 0) fail(pos, "nonzero method-table separator");
            pos += 2;
            for (unsigned i = 0; i < methods; ++i, pos += 2) object.addresses.push_back(word(bytes, pos, end));
            if (pos != end) fail(pos, "unexpected object data");
            objects.push_back(std::move(object));
        } else if (section.type == 7 || section.type == 8) {
            const auto count = word(bytes, pos, end); pos += 2;
            for (unsigned i = 0; i < count; ++i, pos += 2) {
                const auto value = word(bytes, pos, end);
                if (section.type == 7) exports.emplace_back(pos, value);
                else if (value > bytes.size() || bytes.size() - value < 2)
                    fail(pos, "relocation points outside script");
            }
            if (pos != end) fail(pos, "unexpected table data");
        } else if (section.type == 10 && (end - pos) % 2 != 0) {
            fail(pos, "odd-sized locals section");
        }
    }
    std::set<std::size_t> object_pointers;
    for (const auto& object : objects) {
        object_pointers.insert(object.pointer);
        for (auto address : object.addresses) {
            if (boundaries.count(address) == 0) fail(object.pointer, "method target is not an instruction boundary");
            procedures.insert(address);
        }
    }
    for (const auto& entry : exports) {
        if (entry.second == 0) continue;
        if (boundaries.count(entry.second)) procedures.insert(entry.second);
        else if (object_pointers.count(entry.second) == 0) fail(entry.first, "export target is neither code nor object");
    }
    for (const auto& instruction : instructions) {
        const auto op = instruction.opcode;
        if ((op >= 23 && op <= 25) || op == 32) {
            const auto address = target(instruction);
            if (boundaries.count(address) == 0) fail(instruction.start, "branch/call target is not an instruction boundary");
            (op == 32 ? procedures : labels).insert(address);
        }
    }

    return {std::move(sections),std::move(strings),std::move(instructions),std::move(objects),
            std::move(exports),std::move(boundaries),std::move(labels),std::move(procedures),std::move(object_pointers)};
}
} // namespace script_detail

std::string disassemble_sci0_script(const Bytes& bytes, std::optional<std::uint16_t> script_number) {
    using namespace script_detail;
    const auto parsed = parse(bytes);
    const auto& [sections,strings,instructions,objects,exports,boundaries,labels,procedures,object_pointers] = parsed;
    std::ostringstream out;
    out << "; SCIMate SCI0 disassembly: script " << (script_number ? std::to_string(*script_number) : "unspecified") << '\n'
        << "; Inspection only: not decompiled SCI source; import ignores this listing.\n"
        << "; Sectioned SCI0 / 16-bit exports / relative lofsa and lofss.\n"
        << "; Addresses/bytes are hexadecimal; numeric operands are decimal unless prefixed 0x.\n\n";
    for (std::size_t i = 0; i < exports.size(); ++i) {
        const auto address = exports[i].second;
        out << ".export " << i << ", " << (address == 0 ? "null" :
            (object_pointers.count(address) ? "object_" : "proc_") + hex(address)) << '\n';
    }
    for (const auto& object : objects) {
        out << '\n' << (object.is_class ? ".class " : ".instance ") << "object_" << hex(object.pointer);
        if (object.values.size() >= 4 && strings.count(object.values[3])) out << " ; " << strings.at(object.values[3]);
        out << "\n  species " << object.values[0] << ", superclass " << object.values[1]
            << ", info 0x" << hex(object.values[2]) << '\n';
        for (std::size_t i = 0; i < object.values.size(); ++i) {
            out << "  property[" << i << "] = 0x" << hex(object.values[i]);
            if (object.is_class) out << " ; selector_" << object.selectors[i];
            out << '\n';
        }
        for (std::size_t i = 0; i < object.methods.size(); ++i)
            out << "  method selector_" << object.methods[i] << " = proc_" << hex(object.addresses[i]) << '\n';
    }
    std::size_t instruction_index = 0;
    for (const auto& section : sections) {
        out << "\n.section " << section_name(section.type) << " (" << section.type << ") @0x"
            << hex(section.start) << " size " << section.end - section.start << '\n';
        if (section.type == 2) {
            while (instruction_index < instructions.size() && instructions[instruction_index].start < section.end) {
                const auto& instruction = instructions[instruction_index++];
                const auto op = instruction.opcode;
                if (procedures.count(instruction.start)) out << "proc_" << hex(instruction.start) << ":\n";
                if (labels.count(instruction.start)) out << "label_" << hex(instruction.start) << ":\n";
                std::string raw;
                for (auto i = instruction.start; i < instruction.end; ++i) raw += hex(bytes[i], 2) + ' ';
                out << "  " << hex(instruction.start) << "  " << std::left << std::setw(22) << raw << std::right
                    << opcode_name(op);
                for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                    out << (i == 0 ? " " : ", ");
                    const auto value = instruction.operands[i];
                    if (i == 0 && ((op >= 23 && op <= 25) || op == 32))
                        out << (op == 32 ? "proc_" : "label_") << hex(target(instruction));
                    else if (op == 26 || op == 28) out << signed_value(value, instruction.width);
                    else if (i == 0 && (op == 57 || op == 58)) out << "0x" << hex(target(instruction));
                    else if (op == 33 && i == 0) out << "kernel_" << value;
                    else out << value;
                }
                if (op == 57 || op == 58) {
                    const auto address = target(instruction);
                    if (strings.count(address)) out << " ; " << strings.at(address);
                    else if (object_pointers.count(address)) out << " ; object_" << hex(address);
                    else if (address >= bytes.size()) out << " ; WARNING: address outside script";
                }
                out << '\n';
            }
        } else if (section.type == 5) {
            for (const auto& entry : strings)
                if (entry.first >= section.start + 4 && entry.first < section.end)
                    out << "  string_" << hex(entry.first) << ": " << entry.second << '\n';
        } else if (section.type == 10) {
            for (auto pos = section.start + 4; pos < section.end; pos += 2)
                out << "  " << (script_number == 0 ? "global" : "local") << '[' << (pos - section.start - 4) / 2
                    << "] = 0x" << hex(word(bytes, pos, section.end)) << '\n';
        } else {
            // Preserve visibility of said, relocation, synonym, unknown, and object bytes.
            for (auto pos = section.start + 4; pos < section.end; pos += 16) {
                out << "  " << hex(pos) << "  .bytes";
                for (auto i = pos; i < section.end && i < pos + 16; ++i) out << ' ' << hex(bytes[i], 2);
                out << '\n';
            }
        }
    }
    out << "\n.end\n";
    return out.str();
}
} // namespace scimate
