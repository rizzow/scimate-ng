// SPDX-License-Identifier: GPL-2.0-or-later
// Independent layout transform; format references are listed in provenance.md.
#include "scimate/sci11_script.hpp"
#include "script_internal.hpp"
#include <queue>

namespace scimate {
namespace {
using namespace script_detail;
using Addresses = std::map<std::uint16_t,std::uint16_t>;
[[noreturn]] void invalid(const std::string& reason) { throw std::runtime_error("SCI1.1 script/heap transform: " + reason); }
std::uint16_t narrow(std::size_t value) {
    if (value > 65535) invalid("16-bit address/size limit exceeded");
    return static_cast<std::uint16_t>(value);
}
void put(Bytes& bytes, std::size_t value) {
    auto v = narrow(value);
    bytes.push_back(static_cast<std::uint8_t>(v)); bytes.push_back(static_cast<std::uint8_t>(v >> 8));
}
void patch(Bytes& bytes, std::size_t slot, std::size_t value) {
    auto v = narrow(value);
    if (slot > bytes.size() || bytes.size()-slot < 2) invalid("pointer slot outside resource");
    bytes[slot] = static_cast<std::uint8_t>(v); bytes[slot+1] = static_cast<std::uint8_t>(v >> 8);
}
std::uint16_t lookup(const Addresses& addresses, std::size_t old, const char* context) {
    auto found = addresses.find(narrow(old));
    if (found == addresses.end()) invalid(std::string(context) + " at source $" + hex(old) + " has no representable target");
    return found->second;
}
bool rewrite_wide(unsigned op) { return (op >= 23 && op <= 25) || op == 32 || (op >= 49 && op <= 58); }
bool relative_code(unsigned op) { return (op >= 23 && op <= 25) || op == 32; }
std::size_t new_length(const Instruction& inst) {
    return rewrite_wide(inst.opcode) ? inst.end-inst.start+2-inst.width : inst.end-inst.start;
}
// Widen pointer/property/branch operands before laying out code. This avoids
// iterative widening and keeps all address calculations deterministic.
void check_source_flow(const ParsedScript& parsed) {
    std::map<std::size_t,const Instruction*> instructions;
    for (const auto& inst : parsed.instructions) instructions.emplace(inst.start,&inst);
    std::queue<std::size_t> pending;
    std::set<std::size_t> seen;
    for (auto entry : parsed.procedures) pending.push(entry);
    while (!pending.empty()) {
        auto at = pending.front(); pending.pop();
        if (!seen.insert(at).second) continue;
        auto found = instructions.find(at);
        if (found == instructions.end()) invalid("control-flow target outside source code");
        const auto& inst = *found->second;
        if (relative_code(inst.opcode)) pending.push(target(inst));
        if (inst.opcode != 36 && inst.opcode != 25) {
            if (!instructions.count(inst.end)) invalid("reachable fallthrough outside source code at $" + hex(inst.start));
            pending.push(inst.end);
        }
    }
}
unsigned operand_width(unsigned op, unsigned index, unsigned encoded_width) {
    if (op == 35) return index == 2 ? 1 : encoded_width;
    if (op == 32 || op == 33 || op == 34 || op == 43) return index == 1 ? 1 : encoded_width;
    if (op == 37 || op == 42) return 1;
    return encoded_width;
}
void relocations(Bytes& bytes, std::size_t header, const std::set<std::uint16_t>& slots) {
    patch(bytes,header,bytes.size()); put(bytes,slots.size());
    for (auto slot : slots) put(bytes,slot);
}
} // namespace

Sci11ScriptPair split_sci0_script(const Bytes& payload) {
    const auto parsed = parse(payload);
    check_source_flow(parsed);
    const Section* locals = nullptr;
    std::set<unsigned> singleton;
    for (const auto& section : parsed.sections) {
        if (section.type == 3 || section.type == 4) {
            if (section.end != section.start+4) invalid("Said/synonym data requires a parser adaptation and cannot be dropped");
        } else if (section.type == 7 || section.type == 8 || section.type == 10) {
            if (!singleton.insert(section.type).second) invalid("multiple exports/locals/relocation sections are ambiguous");
            if (section.type == 10) locals = &section;
        } else if (section.type == 9) {
            if (section.end != section.start+4) invalid("unsupported preload-text section payload");
        } else if (section.type != 1 && section.type != 2 && section.type != 5 && section.type != 6) {
            invalid("unsupported section type " + std::to_string(section.type));
        }
    }
    Sci11ScriptPair result;
    auto& script = result.script;
    auto& heap = result.heap;
    std::set<std::uint16_t> script_relocations, heap_relocations;
    Addresses heap_slots;
    put(script,0); put(script,0);
    bool preload = false;
    for (const auto& section : parsed.sections) if (section.type == 9) preload = true;
    put(script,preload?1:0); put(script,parsed.exports.size());
    script.resize(script.size()+parsed.exports.size()*2,0);
    put(heap,0);
    const auto local_count = locals ? (locals->end-locals->start-4)/2 : 0;
    put(heap,local_count);
    if (locals) for (auto old = locals->start+4; old < locals->end; old += 2) {
        heap_slots.emplace(narrow(old),narrow(heap.size())); put(heap,word(payload,old,locals->end));
    }
    std::vector<std::pair<std::size_t,std::size_t>> method_patches;
    for (const auto& object : parsed.objects) {
        if (object.values.size() < 3) invalid("object lacks core properties");
        if (object.is_class != ((object.values[2] & 0x8000u) != 0)) invalid("object section and class flag disagree");
        if (!object.is_class && object.values[0] != 0xffff) invalid("instance species must use the standard uninitialized 0xffff value");
        auto object_at = narrow(heap.size());
        result.heap_addresses.emplace(narrow(object.pointer),object_at);
        auto property_count = object.values.size()+5;
        auto property_dictionary = narrow(script.size());
        if (object.is_class) {
            if (object.selectors.size() != object.values.size()) invalid("class selector/property count differs");
            // The five new intrinsic properties use the reserved metadata IDs
            // emitted by SCI1.1 compilers. Existing selector IDs stay unchanged.
            for (auto selector : object.selectors)
                if (selector >= 4096 && selector <= 4100) invalid("source selector collides with new SCI1.1 metadata");
            for (unsigned i = 0; i < 5; ++i) put(script,4096+i);
            for (auto selector : object.selectors) put(script,selector);
        }
        auto method_dictionary = narrow(script.size());
        put(script,object.methods.size());
        for (std::size_t i = 0; i < object.methods.size(); ++i) {
            put(script,object.methods[i]);
            method_patches.emplace_back(script.size(),object.addresses[i]); put(script,0);
        }
        put(heap,0x1234); put(heap,property_count);
        put(heap,property_dictionary); put(heap,method_dictionary); put(heap,0); // Runtime class-script field.
        for (std::size_t i = 0; i < object.values.size(); ++i) {
            heap_slots.emplace(narrow(object.pointer+2*i),narrow(heap.size())); put(heap,object.values[i]);
        }
    }
    put(heap,0); // Object-list terminator; strings follow it.
    for (const auto& section : parsed.sections) if (section.type == 5) {
        for (auto old = section.start+4; old < section.end; ++old) {
            result.heap_addresses.emplace(narrow(old),narrow(heap.size())); heap.push_back(payload[old]);
        }
    }
    auto code_start = script.size();
    for (const auto& inst : parsed.instructions) {
        result.code_addresses.emplace(narrow(inst.start),narrow(code_start)); code_start += new_length(inst);
        narrow(code_start);
    }
    for (const auto& inst : parsed.instructions) {
        auto start = script.size();
        auto width = rewrite_wide(inst.opcode) ? 2u : inst.width;
        script.push_back(static_cast<std::uint8_t>((inst.opcode << 1) | (width == 1 ? 1u : 0u)));
        for (unsigned i = 0; i < inst.operands.size(); ++i) {
            auto value = inst.operands[i];
            auto count = operand_width(inst.opcode,i,width);
            if (i == 0 && relative_code(inst.opcode)) {
                auto destination = lookup(result.code_addresses,target(inst),"branch/call");
                auto delta = static_cast<std::int64_t>(destination)-static_cast<std::int64_t>(start+new_length(inst));
                if (delta < -32768 || delta > 32767) invalid("branch/call exceeds signed 16-bit displacement");
                value = static_cast<std::uint16_t>(delta);
            } else if (i == 0 && (inst.opcode == 57 || inst.opcode == 58)) {
                value = lookup(result.heap_addresses,target(inst),"LOFS pointer");
                script_relocations.insert(narrow(script.size()));
            } else if (i == 0 && inst.opcode >= 49 && inst.opcode <= 56) {
                if (value%2 || value > 65525) invalid("invalid source property byte offset");
                value += 10; // Five newly inserted object fields, each two bytes.
            }
            if (count == 1) script.push_back(static_cast<std::uint8_t>(value)); else put(script,value);
        }
        if (script.size()-start != new_length(inst)) invalid("instruction layout disagreement");
    }
    for (auto entry : method_patches) patch(script,entry.first,lookup(result.code_addresses,entry.second,"method"));
    for (std::size_t i = 0; i < parsed.exports.size(); ++i) {
        auto value = parsed.exports[i].second;
        auto slot = 8+i*2;
        if (!value) continue; // Preserve an empty export slot.
        if (parsed.object_pointers.count(value)) {
            patch(script,slot,lookup(result.heap_addresses,value,"object export")); script_relocations.insert(narrow(slot));
        } else patch(script,slot,lookup(result.code_addresses,value,"procedure export"));
    }
    std::set<std::size_t> old_relocations;
    for (const auto& section : parsed.sections) if (section.type == 8) {
        auto count = word(payload,section.start+4,section.end);
        for (unsigned i = 0; i < count; ++i) old_relocations.insert(word(payload,section.start+6+i*2,section.end));
    }
    // The name field is a format-defined pointer, even when a compiler omitted
    // its relocation entry. No other property is inferred to be a pointer.
    for (const auto& object : parsed.objects)
        if (object.values.size() > 3 && object.values[3]) old_relocations.insert(object.pointer+6);
    for (auto old_slot : old_relocations) {
        auto slot = lookup(heap_slots,old_slot,"heap relocation slot");
        auto old_target = word(payload,old_slot,payload.size());
        patch(heap,slot,lookup(result.heap_addresses,old_target,"heap relocation value")); heap_relocations.insert(slot);
    }
    // ScummVM joins the hunk and heap on an even boundary. Emit an even hunk so
    // the loaded combined address space has exactly the serialized size.
    if (script.size()%2) script.push_back(0);
    relocations(script,0,script_relocations);
    relocations(heap,0,heap_relocations);
    if (script.size()+heap.size() > 65535) invalid("combined script/heap exceeds 64 KiB address space");
    validate_sci11_script_pair(script,heap);
    return result;
}

void validate_sci11_script_pair(const Bytes& script, const Bytes& heap) {
    if (script.size() < 10 || heap.size() < 8 || script.size()%2 || script.size()+heap.size() > 65535)
        invalid("invalid hunk/heap size or combined address space");
    auto read = [](const Bytes& bytes, std::size_t at) -> unsigned {
        if (at > bytes.size() || bytes.size()-at < 2) invalid("truncated word");
        return bytes[at] | (static_cast<unsigned>(bytes[at+1]) << 8);
    };
    auto script_end = read(script,0), heap_end = read(heap,0);
    auto read_relocations = [&](const Bytes& bytes, std::size_t at) {
        auto count = read(bytes,at);
        if (at+2+2*static_cast<std::size_t>(count) != bytes.size()) invalid("relocation table must end at resource EOF");
        std::set<std::uint16_t> slots;
        for (unsigned i = 0; i < count; ++i) {
            auto slot = read(bytes,at+2+2*i);
            if (slot+2 > at || !slots.insert(static_cast<std::uint16_t>(slot)).second) invalid("duplicate or out-of-bounds relocation slot");
        }
        return slots;
    };
    auto script_relocations = read_relocations(script,script_end);
    auto heap_relocations = read_relocations(heap,heap_end);
    const auto exports = read(script,6);
    std::size_t dictionaries = 8+2*static_cast<std::size_t>(exports);
    std::size_t object_at = 4+2*static_cast<std::size_t>(read(heap,2));
    if (dictionaries > script_end || object_at+2 > heap_end) invalid("exports/locals overlap relocation table");
    std::set<std::size_t> objects, heap_slots, heap_targets, script_slots;
    for (std::size_t slot = 4; slot < object_at; slot += 2) heap_slots.insert(slot);
    std::vector<unsigned> methods;
    while (read(heap,object_at) == 0x1234) {
        auto count = read(heap,object_at+2);
        auto end = object_at+2*static_cast<std::size_t>(count);
        if (count < 8 || end+2 > heap_end) invalid("invalid object property count/bounds");
        objects.insert(object_at); heap_targets.insert(object_at);
        const auto species = read(heap,object_at+10), info = read(heap,object_at+14);
        const bool is_class = species != 0xffff;
        if (is_class != ((info & 0x8000u) != 0)) invalid("object species and class flag disagree");
        if (read(heap,object_at+4) != dictionaries) invalid("object property dictionary offset disagrees with layout");
        if (is_class) {
            if (dictionaries+2*count > script_end) invalid("property dictionary overlaps code/relocations");
            for (unsigned i = 0; i < 5; ++i)
                if (read(script,dictionaries+2*i) != 4096+i) invalid("invalid intrinsic property selector");
            dictionaries += 2*count;
        }
        if (read(heap,object_at+6) != dictionaries) invalid("object method dictionary offset disagrees with layout");
        auto method_count = read(script,dictionaries);
        if (dictionaries+2+4*static_cast<std::size_t>(method_count) > script_end) invalid("method dictionary outside hunk");
        for (unsigned i = 0; i < method_count; ++i) methods.push_back(read(script,dictionaries+4+4*i));
        dictionaries += 2+4*method_count;
        for (std::size_t slot = object_at+16; slot < end; slot += 2) heap_slots.insert(slot);
        object_at = end;
    }
    if (read(heap,object_at) != 0) invalid("missing heap object terminator");
    for (auto at = object_at+2; at < heap_end; ++at) heap_targets.insert(at);
    auto string_at = object_at+2;
    while (string_at < heap_end) {
        while (string_at < heap_end && heap[string_at]) ++string_at;
        if (string_at == heap_end) invalid("unterminated heap string");
        ++string_at;
    }
    for (auto slot : heap_relocations)
        if (!heap_slots.count(slot) || !heap_targets.count(read(heap,slot))) invalid("unrepresentable heap pointer relocation");
    std::map<std::size_t,Instruction> instructions;
    for (auto at = dictionaries; at < script_end;) {
        Instruction inst{at,0,static_cast<unsigned>(script[at] >> 1),(script[at]&1u)?1u:2u,{}};
        if (opcode_name(inst.opcode).empty()) invalid("reserved opcode in hunk");
        ++at;
        for (auto width : operand_widths(inst.opcode,inst.width)) {
            if (width > script_end-at) invalid("truncated hunk instruction");
            inst.operands.push_back(width == 1 ? script[at] : read(script,at)); at += width;
        }
        inst.end = at;
        if (inst.opcode == 57 || inst.opcode == 58) {
            if (inst.width != 2) invalid("non-word heap pointer operand");
            script_slots.insert(inst.start+1);
            if (!script_relocations.count(narrow(inst.start+1)) || !heap_targets.count(inst.operands[0])) invalid("LOFS lacks a valid heap relocation");
        }
        if (inst.opcode >= 49 && inst.opcode <= 56 && (inst.operands[0] < 10 || inst.operands[0]%2))
            invalid("invalid shifted property operand");
        instructions.emplace(inst.start,std::move(inst));
    }
    for (auto method : methods) if (!instructions.count(method)) invalid("method is not an instruction boundary");
    for (const auto& entry : instructions)
        if (relative_code(entry.second.opcode) && !instructions.count(target(entry.second))) invalid("branch/call is not an instruction boundary");
    for (unsigned i = 0; i < exports; ++i) {
        auto slot = 8+2*i, value = read(script,slot);
        if (script_relocations.count(narrow(slot))) {
            script_slots.insert(slot);
            if (!objects.count(value)) invalid("object export does not point to an object");
        } else if (value && !instructions.count(value)) invalid("procedure export is not an instruction boundary");
    }
    for (auto slot : script_relocations)
        if (!script_slots.count(slot)) invalid("unrecognized hunk heap-pointer relocation");
}
} // namespace scimate
