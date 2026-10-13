// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDialog>

namespace scimate { struct Project; }

class GameInfoDialog : public QDialog {
public:
    explicit GameInfoDialog(const scimate::Project& project, QWidget* parent = nullptr);
};
