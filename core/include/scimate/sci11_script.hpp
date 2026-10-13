// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"

namespace scimate {
struct Sci11ScriptPair {
    Bytes script; // Hunk: header, exports, dictionaries, code, heap-pointer relocations.
    Bytes heap;   // Header, locals, objects, strings, heap-pointer relocations.
    std::map<std::uint16_t,std::uint16_t> code_addresses;
    std::map<std::uint16_t,std::uint16_t> heap_addresses;
};
// Structural conversion of standard sectioned SCI0 scripts with relative LOFS
// and narrow exports. Does not adapt kernels, parser gameplay, sound or graphics.
// Unsupported/ambiguous source layouts fail explicitly. No game identity rules.
Sci11ScriptPair split_sci0_script(const Bytes& payload);
// Validates the DOS SCI1.1 hunk/heap layout, instruction boundaries, dictionaries,
// exports and pointer relocations. This is not execution/behavioral validation.
void validate_sci11_script_pair(const Bytes& script, const Bytes& heap);
} // namespace scimate
