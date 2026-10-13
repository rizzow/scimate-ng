// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>

class QWidget;
namespace preferences {
QString projectDirectory();
bool animateViews();
bool showDialog(QWidget* parent);
}
