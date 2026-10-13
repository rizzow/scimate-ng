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
    dialog.setMinimumWidth(560);
    dialog.resize(680, 300);
    QVBoxLayout layout(&dialog);
    layout.setContentsMargins(24, 24, 24, 20);
    layout.setSpacing(8);
    auto sectionLabel = [&](const QString& title) {
        auto* label = new QLabel(title);
        auto font = label->font(); font.setBold(true); label->setFont(font);
        layout.addWidget(label);
        return label;
    };
    auto* directoryLabel = sectionLabel("Project directory");
    auto* hint = new QLabel("New game projects are stored in separate subfolders here. Existing projects stay in their current location.");
    hint->setWordWrap(true); layout.addWidget(hint);
    QLineEdit directory(projectDirectory());
    directory.setObjectName("projectDirectory");
    directory.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    directoryLabel->setBuddy(&directory);
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    auto* browse = new QPushButton("Browse…");
    browse->setAutoDefault(false);
    row->addWidget(&directory, 1); row->addWidget(browse);
    layout.addLayout(row);
    layout.addSpacing(16);
    sectionLabel("Resource previews");
    QCheckBox animate("Animate views by default");
    animate.setObjectName("animateViews");
    animate.setChecked(animateViews()); layout.addWidget(&animate);
    layout.addStretch(1);
    layout.addSpacing(16);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
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
