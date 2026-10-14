// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/explain.hpp"
#include <QTreeWidget>
#include <map>

struct ExplanationSelection {
    QString evidence;
    std::optional<scimate::ResourceId> resource;
    std::optional<int> loop;
    std::optional<std::size_t> offset;
};

// Links come from structured analysis, never from parsing display text.
class ExplanationTree : public QTreeWidget {
public:
    explicit ExplanationTree(QWidget* parent = nullptr);
    ~ExplanationTree() override;
    void resetAnalysis();
    void setAnalysis(const scimate::GameExplanation& analysis);
    void focusResource(scimate::ResourceId resource);
    ExplanationSelection selection() const;
private:
    using Key = std::pair<scimate::ResourceType, std::uint16_t>;
    QTreeWidgetItem* resourceNode(scimate::ResourceId resource);
    QTreeWidgetItem* referencesRoot_ = nullptr;
    std::map<Key, QTreeWidgetItem*> resourceNodes_;
};
