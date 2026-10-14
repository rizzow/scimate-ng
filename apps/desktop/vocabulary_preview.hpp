// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <QString>
class QStackedWidget;
QString showVocabularyPreview(QStackedWidget* stack, scimate::ResourceId id, const scimate::Bytes& payload);
