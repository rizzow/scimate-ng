// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/diagnostics.hpp"
#include <QMessageBox>
namespace desktop_detail {
inline void show_error(QWidget* parent, const QString& operation, const QString& message,
                       scimate::LogLevel level = scimate::LogLevel::error) {
    scimate::log_diagnostic(level, operation.toUtf8().toStdString(), message.toUtf8().toStdString());
    QMessageBox box(level == scimate::LogLevel::warning ? QMessageBox::Warning : QMessageBox::Critical,
                    operation, message, QMessageBox::Ok, parent);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}
inline void show_exception(QWidget* parent, const QString& operation,
                           scimate::LogLevel level = scimate::LogLevel::error) {
    show_error(parent, operation, QString::fromStdString(scimate::exception_message(std::current_exception())), level);
}
}
