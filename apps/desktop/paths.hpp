// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/workspace.hpp"
#include <QtWidgets>
namespace desktop_detail {
inline std::filesystem::path path(const QString& text) { return std::filesystem::u8path(text.toUtf8().constData()); }
inline QString text(const std::filesystem::path& p) { return QString::fromUtf8(p.u8string().c_str()); }
inline QString stem(std::size_t index) { return QString::fromStdString(scimate::workspace_detail::record_stem(index)); }
inline QString dialogDirectory(const QString& key) {
    const auto saved = QSettings().value("dialogs/" + key).toString();
    return !saved.isEmpty() && QDir(saved).exists() ? saved : QDir::homePath();
}
inline void rememberDirectory(const QString& key, const QString& directory) {
    if (!directory.isEmpty() && QDir(directory).exists())
        QSettings().setValue("dialogs/" + key, QDir(directory).absolutePath());
}
}
