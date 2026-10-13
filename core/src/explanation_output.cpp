// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/explain.hpp"
#include "json.hpp"
#include <functional>
#include <sstream>
#include <set>

namespace scimate {
namespace {
using json_detail::Json;
const char* confidence(ExplanationConfidence value) {
    switch (value) {
    case ExplanationConfidence::fact: return "fact";
    case ExplanationConfidence::likely: return "likely";
    case ExplanationConfidence::unresolved: return "unresolved";
    case ExplanationConfidence::none: return "none";
    }
    return "none";
}
bool matches(ResourceId a, ResourceId b) { return a.type == b.type && a.number == b.number; }
Json resource(ResourceId id) {
    return Json::Object{{"type", std::string(resource_type_name(id.type))}, {"number", id.number}};
}
Json nodeJson(const ExplanationNode& node) {
    Json::Array children;
    for (const auto& child : node.children) children.push_back(nodeJson(child));
    return Json::Object{{"text", node.text}, {"confidence", confidence(node.confidence)},
        {"resource", node.resource ? resource(*node.resource) : Json{}},
        {"offset", node.offset ? Json(*node.offset) : Json{}},
        {"loop", node.loop ? Json(*node.loop) : Json{}}, {"children", std::move(children)}};
}
}

std::string format_explanation_json(const GameExplanation& report) {
    std::set<std::pair<ResourceType, std::uint16_t>> present;
    for (const auto& id : report.resources) present.emplace(id.type, id.number);
    Json::Array nodes, references, resources, diagnostics;
    for (const auto& node : report.nodes) nodes.push_back(nodeJson(node));
    for (const auto& id : report.resources) resources.push_back(resource(id));
    for (const auto& diagnostic : report.diagnostics) diagnostics.emplace_back(diagnostic);
    for (const auto& ref : report.references)
        references.emplace_back(Json::Object{{"source", resource(ref.source)}, {"target", resource(ref.target)},
            {"offset", ref.offset}, {"confidence", ref.confidence == ReferenceConfidence::decoded ? "fact" : "likely"},
            {"evidence", ref.evidence}, {"target_exists", bool(present.count({ref.target.type, ref.target.number}))}});
    return json_detail::dump_json(Json::Object{{"schema_version", 1}, {"format", "sci0"}, {"analysis", "static"},
        {"focus", report.focus ? resource(*report.focus) : Json{}}, {"shadowed_entries", report.shadowed_entries},
        {"resources", std::move(resources)}, {"nodes", std::move(nodes)},
        {"references", std::move(references)}, {"diagnostics", std::move(diagnostics)}});
}

std::string format_explanation_text(const GameExplanation& report) {
    std::set<std::pair<ResourceType, std::uint16_t>> present;
    for (const auto& id : report.resources) present.emplace(id.type, id.number);
    std::ostringstream out;
    out << "SCI0 explanation (static heuristics; game execution is not simulated)\n"
        << "Labels: [fact] decoded data; [likely] convention/pattern; [unresolved] insufficient evidence.\n"
        << "Duplicate resource IDs: " << report.shadowed_entries << " shadowed entries; first map entry wins.\n";
    std::function<void(const ExplanationNode&, unsigned)> write = [&](const auto& node, unsigned depth) {
        out << std::string(depth * 2, ' ');
        if (node.confidence != ExplanationConfidence::none) out << '[' << confidence(node.confidence) << "] ";
        out << node.text << '\n';
        for (const auto& child : node.children) write(child, depth + 1);
    };
    for (const auto& node : report.nodes) { out << '\n'; write(node, 0); }
    out << "\nResource references (static uses; execution not proven)\n";
    for (const auto& ref : report.references) {
        if (report.focus && !matches(*report.focus, ref.source) && !matches(*report.focus, ref.target)) continue;
        out << "  [" << (ref.confidence == ReferenceConfidence::decoded ? "fact" : "likely") << "] "
            << resource_type_name(ref.source.type) << ' ' << ref.source.number << " -> "
            << resource_type_name(ref.target.type) << ' ' << ref.target.number
            << (bool(present.count({ref.target.type, ref.target.number})) ? "" : " (resource absent)") << ": " << ref.evidence << '\n';
    }
    if (!report.diagnostics.empty()) {
        out << "\nDiagnostics (partial analysis)\n";
        for (const auto& diagnostic : report.diagnostics) out << "  " << diagnostic << '\n';
    }
    return out.str();
}
}
