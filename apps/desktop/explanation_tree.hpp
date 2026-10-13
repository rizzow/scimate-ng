// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_map.hpp"
#include <QTreeWidget>
#include <optional>

struct ExplanationSelection {
    QString evidence;
    std::optional<scimate::ResourceId> resource;
    std::optional<int> loop;
};

// Presentation adapter for the CLI report. Only explicit resource references
// become links; room numbers and movement directions are not resource IDs.
class ExplanationTree : public QTreeWidget {
public:
    explicit ExplanationTree(QWidget* parent = nullptr);
    void setReport(const QString& report);
    ExplanationSelection selection() const;
};
