// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QStackedWidget>

// Keep just one specialized preview; switching resources releases its data.
inline void clearResourcePreview(QStackedWidget* stack) {
    if (auto* old = stack->findChild<QWidget*>("assetPreview", Qt::FindDirectChildrenOnly)) {
        stack->removeWidget(old);
        delete old;
    }
}
inline void installResourcePreview(QStackedWidget* stack, QWidget* page) {
    clearResourcePreview(stack);
    page->setObjectName("assetPreview");
    stack->addWidget(page);
    stack->setCurrentWidget(page);
    stack->show();
}
