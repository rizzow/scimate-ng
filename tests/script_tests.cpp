// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "scimate/script.hpp"
#include <iostream>

namespace {
using namespace fixture;
const Bytes script{
    7,0,8,0,1,0,12,0, // export 0 -> 12
    2,0,18,0,
    0x35,0xff,         // 12: ldi -1
    0x31,5,            // 14: bnt 21
    0x41,3,0,          // 16: call 22 (base includes argument byte)
    0x33,0xf7,         // 19: jmp 12
    0x48,              // 21: ret
    0x38,0xfe,0xff,    // 22: pushi -2 (word)
    0x48,              // 25: ret
    5,0,8,0,'h','i',0,0,
    10,0,8,0,2,0,0xff,0xff,
    0,0
};
void contains(const std::string& text, const char* expected) {
    require(text.find(expected) != std::string::npos, expected);
}
void tests() {
    const auto text = disassemble_sci0_script(script, 42);
    contains(text, "script 42");
    contains(text, ".export 0, proc_000c");
    contains(text, "ldi -1");
    contains(text, "bnt label_0015");
    contains(text, "call proc_0016, 0");
    contains(text, "jmp label_000c");
    contains(text, "pushi -2");
    contains(text, "string_001e: \"hi\"");
    contains(text, "local[1] = 0xffff");
    contains(text, "not decompiled SCI source");
    require(disassemble_sci0_script(script, 42) == text, "nondeterministic listing");
    contains(disassemble_sci0_script({2,0,7,0,0x73,5,0x48,5,0,7,0,'h','i',0,0,0}), "lofsa 0x000b ; \"hi\"");
    // Wide callk operand with a fixed byte argument count; followed by ret.
    contains(disassemble_sci0_script({2,0,9,0,0x42,0x34,0x12,2,0x48,0,0}), "callk kernel_4660, 2");
    contains(disassemble_sci0_script({2,0,10,0,0x46,1,0,2,0,0,0,0}), "calle 1, 2, 0");
    contains(disassemble_sci0_script({2,0,7,0,0xff,7,0x48,0,0}), "-spi 7");
    contains(disassemble_sci0_script({2,0,9,0,0x35,1,0x32,0xfb,0xff,0,0}), "jmp label_0004");
    contains(disassemble_sci0_script({2,0,9,0,0x40,0,0,0,0x48,0,0}), "call proc_0008, 0");
    contains(disassemble_sci0_script({2,0,6,0,0x33,4,2,0,5,0,0x48,0,0}), "label_000a:");
    contains(disassemble_sci0_script({5,0,10,0,'"','\\','\n',0xff,'x',0,0,0}), "\\\"\\\\\\n\\xffx");
    // A class with three properties, selectors, and one method at byte 36.
    Bytes object;
    for (unsigned value : {6u,32u,0x1234u,0u,0u,3u,1u,0xffffu,0x8000u,0u,1u,2u,1u,42u,0u,36u}) put(object,value,2);
    object.insert(object.end(), {2,0,5,0,0x48,0,0});
    const auto object_text = disassemble_sci0_script(object);
    contains(object_text, ".class object_000c");
    contains(object_text, "method selector_42 = proc_0024");
    contains(object_text, "proc_0024:");
    contains(disassemble_sci0_script({0,0}), ".end");
    // Only the final export table is active; a stale earlier target may point
    // inside an instruction without invalidating the active table.
    contains(disassemble_sci0_script({7,0,8,0,1,0,13,0,
        2,0,7,0,0x35,0,0x48,7,0,8,0,1,0,14,0,0,0}), ".export 0, proc_000e");

    for (std::size_t cut = 0; cut < script.size(); ++cut)
        fails([&] { disassemble_sci0_script(Bytes(script.begin(), script.begin() + static_cast<std::ptrdiff_t>(cut))); }, "SCI0 script at byte");
    fails([] { disassemble_sci0_script({2,0,3,0,0,0}); }, "section size");
    fails([] { disassemble_sci0_script({2,0,5,0,0x34,0,0}); }, "truncated instruction");
    for (unsigned op : {38u,39u,41u,47u,63u})
        fails([&] { disassemble_sci0_script({2,0,5,0,static_cast<std::uint8_t>(op*2),0,0}); }, "reserved");
    auto bad = script; bad[15] = 1;
    fails([&] { disassemble_sci0_script(bad); }, "instruction boundary");
    bad = script; bad[6] = 13;
    fails([&] { disassemble_sci0_script(bad); }, "export target");
    bad = object; bad[30] = 35;
    fails([&] { disassemble_sci0_script(bad); }, "method target");
    fails([] { disassemble_sci0_script({5,0,5,0,'x',0,0}); }, "unterminated string");
    fails([] { disassemble_sci0_script({8,0,8,0,1,0,0xff,0xff,0,0}); }, "relocation");
    fails([] { disassemble_sci0_script({0,0,1}); }, "after section terminator");
    fails([] { disassemble_sci0_script(Bytes(65536)); }, "size limit");

    std::uint32_t state = 0xabcdef;
    for (unsigned i = 0; i < 3000; ++i) {
        Bytes mutated = script;
        state = state * 1664525u + 1013904223u;
        mutated[state % mutated.size()] = static_cast<std::uint8_t>(state >> 24);
        try { disassemble_sci0_script(mutated); }
        catch (const std::runtime_error&) { /* Rejection is expected; sanitizers check safety. */ }
    }
}
} // namespace
int main() {
    try { tests(); std::cout << "SCI0 script tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
