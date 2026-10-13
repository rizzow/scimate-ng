// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <optional>
#include <string>

namespace scimate {
enum class ReferenceConfidence { decoded, likely };
struct ResourceReference {
    ResourceId source;
    ResourceId target;
    std::size_t offset; // Byte offset in the source script.
    ReferenceConfidence confidence;
    std::string evidence;
};
enum class ExplanationConfidence { none, fact, likely, unresolved };
struct ExplanationNode {
    std::string text;
    ExplanationConfidence confidence = ExplanationConfidence::none;
    std::optional<ResourceId> resource;
    std::optional<std::size_t> offset;
    std::optional<unsigned> loop;
    std::vector<ExplanationNode> children;
};
struct GameExplanation {
    std::vector<ExplanationNode> nodes;
    std::vector<ResourceReference> references;
    std::vector<std::string> diagnostics;
    std::vector<ResourceId> resources; // Effective IDs; last duplicate wins.
    std::size_t shadowed_entries = 0;
    std::optional<ResourceId> focus;
};
std::string format_explanation_text(const GameExplanation& explanation);
std::string format_explanation_json(const GameExplanation& explanation);
// References describe static uses, not proven execution. Dynamic/unsupported
// uses may be absent; a missing reverse lookup never proves a resource unused.
GameExplanation analyze_sci0_game(const Game& game,
                                 std::optional<ResourceId> focus = std::nullopt);
// Static, evidence-backed SCI0 inspection. Missing symbols, malformed individual
// scripts/views, and unresolved dynamic behavior are reported in the output.
// Duplicate resource IDs use their last map entry. Optional focus supports
// any resource type. Reference indexing always considers all scripts.
std::string explain_sci0_game(const Game& game,
                             std::optional<ResourceId> focus = std::nullopt);
} // namespace scimate
