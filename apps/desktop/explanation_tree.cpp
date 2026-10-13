// SPDX-License-Identifier: GPL-2.0-or-later
#include "explanation_tree.hpp"
#include <QSignalBlocker>
#include <functional>
#include <set>

namespace {
enum Role { ResourceTypeRole = Qt::UserRole, ResourceNumberRole, OffsetRole, LoopRole };
QString resourceName(scimate::ResourceId id) {
    return QString::fromStdString(std::string(scimate::resource_type_name(id.type))) + " " + QString::number(id.number);
}
void link(QTreeWidgetItem* item, scimate::ResourceId id, std::optional<std::size_t> offset = {}) {
    item->setData(0, ResourceTypeRole, static_cast<int>(id.type));
    item->setData(0, ResourceNumberRole, id.number);
    if (offset) item->setData(0, OffsetRole, static_cast<qulonglong>(*offset));
    item->setToolTip(0, "Select to preview " + resourceName(id) + ". Double-click to locate it in Resources.");
}
QString label(const scimate::ExplanationNode& node) {
    using Confidence = scimate::ExplanationConfidence;
    const auto prefix = node.confidence == Confidence::fact ? "[fact] " : node.confidence == Confidence::likely ? "[likely] " :
        node.confidence == Confidence::unresolved ? "[unresolved] " : "";
    return prefix + QString::fromStdString(node.text);
}
}

ExplanationTree::ExplanationTree(QWidget* parent) : QTreeWidget(parent) {
    setObjectName("explanationTree");
    setHeaderLabel("Static analysis · fact / likely / unresolved");
    setWordWrap(true);
}

QTreeWidgetItem* ExplanationTree::resourceNode(scimate::ResourceId resource) {
    const Key key{resource.type, resource.number};
    auto found = resourceNodes_.find(key);
    if (found != resourceNodes_.end()) return found->second;
    auto* node = new QTreeWidgetItem(referencesRoot_, {resourceName(resource)});
    link(node, resource);
    new QTreeWidgetItem(node, {"References"});
    new QTreeWidgetItem(node, {"Referenced by"});
    resourceNodes_[key] = node;
    return node;
}

void ExplanationTree::resetAnalysis() {
    const QSignalBlocker selectionSignals(this);
    resourceNodes_.clear(); referencesRoot_ = nullptr;
    QTreeWidget::clear();
}

void ExplanationTree::setAnalysis(const scimate::GameExplanation& analysis) {
    const QSignalBlocker selectionSignals(this);
    resetAnalysis();
    setUpdatesEnabled(false);
    std::function<void(const scimate::ExplanationNode&, QTreeWidgetItem*)> append = [&](const auto& node, auto* parent) {
        auto* item = new QTreeWidgetItem(parent, {label(node)});
        if (node.resource) link(item, *node.resource, node.offset);
        if (node.loop) item->setData(0, LoopRole, *node.loop);
        for (const auto& child : node.children) append(child, item);
    };
    for (const auto& node : analysis.nodes) append(node, invisibleRootItem());
    referencesRoot_ = new QTreeWidgetItem(this, {"Resource references"});
    for (const auto& ref : analysis.references) {
        const auto prefix = ref.confidence == scimate::ReferenceConfidence::decoded ? "[fact] " : "[likely] ";
        const auto evidence = QString::fromStdString(ref.evidence);
        auto* outgoing = new QTreeWidgetItem(resourceNode(ref.source)->child(0), {prefix + resourceName(ref.target) + " — " + evidence});
        link(outgoing, ref.target);
        auto* origin = new QTreeWidgetItem(outgoing, {"Source: " + evidence});
        link(origin, ref.source, ref.offset);
        auto* incoming = new QTreeWidgetItem(resourceNode(ref.target)->child(1), {prefix + evidence});
        link(incoming, ref.source, ref.offset);
        auto* target = new QTreeWidgetItem(incoming, {"Target: " + resourceName(ref.target)});
        link(target, ref.target);
    }
    std::set<Key> present;
    for (const auto& id : analysis.resources) present.emplace(id.type, id.number);
    for (const auto& entry : resourceNodes_) {
        const scimate::ResourceId id{entry.first.first, entry.first.second};
        if (!present.count(entry.first)) entry.second->setText(0, resourceName(id) + " (resource absent)");
        for (int i = 0; i < 2; ++i) {
            auto* group = entry.second->child(i);
            group->setText(0, group->text(0) + QString(" (%1)").arg(group->childCount()));
            if (!group->childCount()) new QTreeWidgetItem(group, {"No recognized static references. Dynamic or unsupported uses may exist."});
        }
    }
    if (!analysis.diagnostics.empty()) {
        auto* diagnostics = new QTreeWidgetItem(this, {"Diagnostics (partial analysis)"});
        for (const auto& text : analysis.diagnostics) new QTreeWidgetItem(diagnostics, {QString::fromStdString(text)});
    }
    expandToDepth(0); setUpdatesEnabled(true);
    if (topLevelItemCount()) setCurrentItem(topLevelItem(0));
}

void ExplanationTree::focusResource(scimate::ResourceId resource) {
    if (!referencesRoot_) return;
    auto* node = resourceNode(resource);
    for (int i = 0; i < 2; ++i) {
        auto* group = node->child(i);
        if (!group->childCount()) new QTreeWidgetItem(group, {"No recognized static references. Dynamic or unsupported uses may exist."});
        group->setExpanded(true);
    }
    referencesRoot_->setExpanded(true); node->setExpanded(true);
    setCurrentItem(node); scrollToItem(node, QAbstractItemView::PositionAtTop);
}

ExplanationSelection ExplanationTree::selection() const {
    ExplanationSelection result;
    auto* selected = currentItem();
    if (!selected) return result;
    std::function<void(QTreeWidgetItem*, int)> append = [&](auto* node, int depth) {
        result.evidence += QString(depth * 2, QChar(' ')) + node->text(0) + '\n';
        for (int i = 0; i < node->childCount(); ++i) append(node->child(i), depth + 1);
    };
    append(selected, 0);
    for (auto* item = selected; item; item = item->parent()) {
        if (!item->data(0, ResourceTypeRole).isValid()) continue;
        result.resource = scimate::ResourceId{static_cast<scimate::ResourceType>(item->data(0, ResourceTypeRole).toInt()),
            static_cast<std::uint16_t>(item->data(0, ResourceNumberRole).toUInt())};
        if (item->data(0, OffsetRole).isValid()) result.offset = item->data(0, OffsetRole).toULongLong();
        if (item->data(0, LoopRole).isValid()) result.loop = item->data(0, LoopRole).toInt();
        break;
    }
    return result;
}
