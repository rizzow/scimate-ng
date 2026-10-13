// SPDX-License-Identifier: GPL-2.0-or-later
#include "explanation_tree.hpp"
#include <QRegularExpression>
#include <functional>
#include <vector>

namespace {
std::optional<scimate::ResourceId> reference(const QString& line) {
    using scimate::ResourceType;
    static const QRegularExpression header("^\\[fact\\] (Script|View) ([0-9]+):");
    static const QRegularExpression property("^\\[fact\\] Initial (view|pic|picture|picNumber) = ([0-9]+)$");
    static const QRegularExpression picture("^\\[likely\\] Background picture reference ([0-9]+) ");
    static const QRegularExpression script(
        "^\\[(?:fact|likely)\\] (?:Class |Instance |Referenced: |Player view: |Player object: |"
        "(?:Opening-room candidate|Constant room call) [0-9]+: )?script ([0-9]+) @ \\$");
    auto id = [](ResourceType type, const QString& value) -> std::optional<scimate::ResourceId> {
        bool valid = false;
        const auto number = value.toUInt(&valid);
        if (!valid || number > 2047) return std::nullopt;
        return scimate::ResourceId{type, static_cast<std::uint16_t>(number)};
    };
    auto match = header.match(line);
    if (match.hasMatch()) return id(match.captured(1) == "Script" ? ResourceType::script : ResourceType::view, match.captured(2));
    match = property.match(line);
    if (match.hasMatch()) return id(match.captured(1) == "view" ? ResourceType::view : ResourceType::pic, match.captured(2));
    match = picture.match(line);
    if (match.hasMatch()) return id(ResourceType::pic, match.captured(1));
    match = script.match(line);
    if (match.hasMatch()) return id(ResourceType::script, match.captured(1));
    return std::nullopt;
}
}

ExplanationTree::ExplanationTree(QWidget* parent) : QTreeWidget(parent) {
    setObjectName("explanationTree");
    setHeaderLabel("Static analysis · fact / likely / unresolved");
    setWordWrap(true);
}

void ExplanationTree::setReport(const QString& report) {
    clear();
    setUpdatesEnabled(false);
    std::vector<std::pair<int, QTreeWidgetItem*>> parents;
    for (const auto& line : report.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        int indent = 0;
        while (indent < line.size() && line[indent] == QChar(' ')) ++indent;
        while (!parents.empty() && parents.back().first >= indent) parents.pop_back();
        auto* item = new QTreeWidgetItem(QStringList{line.trimmed()});
        if (parents.empty()) addTopLevelItem(item);
        else parents.back().second->addChild(item);
        parents.emplace_back(indent, item);
    }
    expandToDepth(0);
    setUpdatesEnabled(true);
    if (topLevelItemCount()) setCurrentItem(topLevelItem(0));
}

ExplanationSelection ExplanationTree::selection() const {
    ExplanationSelection result;
    auto* selected = currentItem();
    if (!selected) return result;
    std::function<void(QTreeWidgetItem*, int)> append = [&](QTreeWidgetItem* node, int depth) {
        result.evidence += QString(depth * 2, QChar(' ')) + node->text(0) + '\n';
        for (int i = 0; i < node->childCount(); ++i) append(node->child(i), depth + 1);
    };
    append(selected, 0);
    // Metadata such as loop counts belongs to the enclosing resource. Explicit
    // picture/view properties take precedence over the owning script.
    for (auto* item = selected; item; item = item->parent()) {
        result.resource = reference(item->text(0));
        if (result.resource) break;
    }
    if (result.resource && result.resource->type == scimate::ResourceType::view) {
        static const QRegularExpression loop("^\\[fact\\] Loop ([0-9]+):");
        const auto match = loop.match(selected->text(0));
        if (match.hasMatch()) result.loop = match.captured(1).toInt();
    }
    return result;
}
