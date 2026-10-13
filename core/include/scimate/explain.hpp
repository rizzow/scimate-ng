// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <optional>
#include <string>

namespace scimate {
// Static, evidence-backed SCI0 inspection. Missing symbols, malformed individual
// scripts/views, and unresolved dynamic behavior are reported in the output.
// Duplicate resource IDs use their last map entry. Optional focus supports
// script and view resources; other resource types are rejected.
std::string explain_sci0_game(const Game& game,
                             std::optional<ResourceId> focus = std::nullopt);
} // namespace scimate
