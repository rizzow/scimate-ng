// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "script_internal.hpp"
#include "sci_vm.hpp"
#include <initializer_list>
#include <limits>
#include <string>

namespace scimate::parser_detail {
// Generates an independent sectioned SCI script. Source game code is never
// embedded. Labels use wide offsets; data pointers live in relocated locals,
// avoiding dependence on the game's early/late LOFS convention.
class ScriptBuilder {
public:
    using Label = std::size_t;
    Label label() { labels_.push_back(unbound); return labels_.size()-1; }
    void bind(Label label) {
        if (labels_.at(label) != unbound) fail("label bound twice");
        labels_[label] = code_.size();
    }
    void export_procedure(Label label) { exports_.push_back(label); }
    unsigned local(unsigned value = 0) {
        checked_word(value);
        const auto index = static_cast<unsigned>(locals_.size());
        locals_.push_back(value); return index;
    }
    void set_local(unsigned index, unsigned value) { checked_word(value); locals_.at(index) = value; }
    unsigned string_local(const std::string& value) {
        if (value.find('\0') != std::string::npos) fail("embedded null in string");
        const auto index = local();
        pointers_.push_back({index,strings_.size()});
        strings_.insert(strings_.end(),value.begin(),value.end()); strings_.push_back(0);
        return index;
    }
    void emit(unsigned opcode, std::initializer_list<unsigned> operands = {}) {
        if (opcode > 127 || script_detail::opcode_name(opcode).empty()) fail("unknown opcode");
        const auto widths = script_detail::operand_widths(opcode,sci_vm::word_bytes);
        if (widths.size() != operands.size()) fail("instruction operand count mismatch");
        code_.push_back(static_cast<std::uint8_t>(sci_vm::encoded_opcode(opcode)));
        auto width = widths.begin();
        for (const auto operand : operands) {
            const auto bytes = *width++;
            if (operand > (bytes == 1 ? 255u : sci_vm::max_script_address)) fail("instruction operand overflow");
            code_.push_back(static_cast<std::uint8_t>(operand));
            if (bytes == sci_vm::word_bytes) code_.push_back(static_cast<std::uint8_t>(operand >> 8));
        }
    }
    void branch(unsigned opcode, Label label) {
        if (!sci_vm::opcode::is_branch(opcode)) fail("non-branch opcode in label fixup");
        const auto start = code_.size(); emit(opcode,{0});
        fixups_.push_back({start+1,code_.size(),label});
    }
    void call(Label label, unsigned parameter_count) {
        const auto start = code_.size();
        emit(sci_vm::opcode::call_local,{0,parameter_count*sci_vm::word_bytes});
        fixups_.push_back({start+1,code_.size(),label});
    }
    std::size_t serialized_size() const {
        std::size_t size = sci_vm::section_header_bytes+sci_vm::word_bytes*(1+exports_.size());
        size += sci_vm::section_header_bytes+code_.size()+sci_vm::word_bytes;
        if (!strings_.empty()) size += sci_vm::section_header_bytes+strings_.size();
        if (!locals_.empty()) size += sci_vm::section_header_bytes+locals_.size()*sci_vm::word_bytes;
        if (!pointers_.empty()) size += sci_vm::section_header_bytes+sci_vm::word_bytes*(1+pointers_.size());
        return size;
    }
    void account_component(const std::string& name) {
        const auto size = serialized_size();
        components_.push_back({name,size-accounted_size_}); accounted_size_ = size;
    }
    std::string size_report() const {
        std::string report;
        for (const auto& component : components_) {
            if (!report.empty()) report += ", ";
            report += component.first+"="+std::to_string(component.second)+" bytes";
        }
        return report;
    }
    Bytes finish() const {
        if (exports_.empty() || code_.empty()) fail("script has no exported code");
        if (serialized_size() > sci_vm::max_script_address) fail("script exceeds address space");
        auto code = code_;
        for (const auto& fixup : fixups_) {
            const auto target = bound(fixup.label);
            const auto delta = static_cast<std::int64_t>(target)-static_cast<std::int64_t>(fixup.end);
            if (delta < -32768 || delta > 32767) fail("relative branch/call span exceeds signed word");
            patch(code,fixup.operand,static_cast<std::uint16_t>(delta));
        }
        Bytes bytes;
        const auto exports_size = sci_vm::section_header_bytes+sci_vm::word_bytes*(1+exports_.size());
        const auto code_start = exports_size+sci_vm::section_header_bytes;
        append_section_header(bytes,sci_vm::section::exports,exports_size);
        put(bytes,exports_.size());
        for (const auto label : exports_) {
            const auto target = bound(label);
            if (target >= code.size()) fail("export points beyond code");
            put(bytes,code_start+target);
        }
        append_section_header(bytes,sci_vm::section::code,sci_vm::section_header_bytes+code.size());
        bytes.insert(bytes.end(),code.begin(),code.end());
        std::size_t strings_start = 0;
        if (!strings_.empty()) {
            strings_start = bytes.size()+sci_vm::section_header_bytes;
            append_section_header(bytes,sci_vm::section::strings,sci_vm::section_header_bytes+strings_.size());
            bytes.insert(bytes.end(),strings_.begin(),strings_.end());
        }
        auto locals = locals_;
        for (const auto& pointer : pointers_) locals.at(pointer.local) = static_cast<unsigned>(strings_start+pointer.offset);
        const auto locals_start = bytes.size()+sci_vm::section_header_bytes;
        if (!locals.empty()) {
            append_section_header(bytes,sci_vm::section::locals,sci_vm::section_header_bytes+locals.size()*sci_vm::word_bytes);
            for (const auto value : locals) put(bytes,value);
        }
        if (!pointers_.empty()) {
            append_section_header(bytes,sci_vm::section::relocations,sci_vm::section_header_bytes+sci_vm::word_bytes*(1+pointers_.size()));
            put(bytes,pointers_.size());
            for (const auto& pointer : pointers_) put(bytes,locals_start+pointer.local*sci_vm::word_bytes);
        }
        put(bytes,0);
        if (bytes.size() > sci_vm::max_script_address) fail("script exceeds address space");
        script_detail::parse(bytes); // Structural verification, performed by the exporter.
        return bytes;
    }
private:
    static constexpr std::size_t unbound = std::numeric_limits<std::size_t>::max();
    struct Fixup { std::size_t operand, end; Label label; };
    struct Pointer { unsigned local; std::size_t offset; };
    Bytes code_, strings_;
    std::vector<std::size_t> labels_;
    std::vector<Label> exports_;
    std::vector<unsigned> locals_;
    std::vector<Fixup> fixups_;
    std::vector<Pointer> pointers_;
    std::vector<std::pair<std::string,std::size_t>> components_;
    std::size_t accounted_size_ = 0;
    [[noreturn]] static void fail(const char* message) { throw std::runtime_error(std::string("portable parser bytecode: ")+message); }
    static void checked_word(std::size_t value) { if (value > sci_vm::max_script_address) fail("word overflow"); }
    static void put(Bytes& bytes, std::size_t value) {
        checked_word(value); bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }
    static void patch(Bytes& bytes, std::size_t offset, unsigned value) {
        checked_word(value); bytes.at(offset) = static_cast<std::uint8_t>(value); bytes.at(offset+1) = static_cast<std::uint8_t>(value >> 8);
    }
    static void append_section_header(Bytes& bytes, unsigned type, std::size_t size) { put(bytes,type); put(bytes,size); }
    std::size_t bound(Label label) const {
        const auto value = labels_.at(label);
        if (value == unbound) fail("unbound label");
        return value;
    }
};
} // namespace scimate::parser_detail
