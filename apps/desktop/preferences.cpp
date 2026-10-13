// SPDX-License-Identifier: GPL-2.0-or-later
#include "preferences.hpp"
#include <QtWidgets>

namespace preferences {
QString projectDirectory() {
    const auto saved = QSettings().value("preferences/projectDirectory").toString();
    return saved.isEmpty() ? QDir::home().filePath("SCIMate Projects") : saved;
}

bool animateViews() {
    return QSettings().value("preferences/animateViews", false).toBool();
}

bool showDialog(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Preferences");
    dialog.resize(600, 200);
    QFormLayout layout(&dialog);
    QLineEdit directory(projectDirectory());
    directory.setObjectName("projectDirectory");
    auto* row = new QHBoxLayout;
    auto* browse = new QPushButton("Browse…");
    row->addWidget(&directory, 1); row->addWidget(browse);
    layout.addRow("Project directory", row);
    auto* hint = new QLabel("New game projects are stored in separate subfolders here. Existing projects stay in their current location.");
    hint->setWordWrap(true); layout.addRow(hint);
    QCheckBox animate("Animate views by default");
    animate.setObjectName("animateViews");
    animate.setChecked(animateViews()); layout.addRow(&animate);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout.addRow(&buttons);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const auto start = QDir(directory.text()).exists() ? directory.text() : QDir::homePath();
        const auto selected = QFileDialog::getExistingDirectory(&dialog, "Project directory", start);
        if (!selected.isEmpty()) directory.setText(selected);
    });
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto value = directory.text().trimmed();
        if (value == "~") value = QDir::homePath();
        else if (value.startsWith("~/")) value = QDir::home().filePath(value.mid(2));
        const QFileInfo info(value);
        if (value.isEmpty() || !QDir::isAbsolutePath(value) || (info.exists() && !info.isDir())) {
            QMessageBox::warning(&dialog, "Invalid project directory", "Enter an absolute folder path, or choose a folder with Browse.");
            return;
        }
        QSettings settings;
        settings.setValue("preferences/projectDirectory", QDir::cleanPath(value));
        settings.setValue("preferences/animateViews", animate.isChecked());
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
}
