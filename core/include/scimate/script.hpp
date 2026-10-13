// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <optional>
#include <string>

namespace scimate {
// Standard sectioned SCI0 scripts, 16-bit exports, relative lofsa/lofss.
// Returns deterministic assembly-style text, NOT decompiled/compilable source.
// No filesystem, vocabulary, or source-file dependencies. Unknown names use IDs.
// Throws std::runtime_error with a byte offset for malformed/unsupported input.
std::string disassemble_sci0_script(const Bytes& payload,
                                  std::optional<std::uint16_t> script_number = std::nullopt);
} // namespace scimate
