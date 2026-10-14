// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.hpp"
#include "room_explorer.hpp"
#include "explanation_tree.hpp"
#include "preferences.hpp"
#include "game_info.hpp"
#include "diagnostics.hpp"
#include "paths.hpp"
#include "scimate/script.hpp"
#include "scimate/asset_io.hpp"
#include "scimate/explain.hpp"
#include "scimate/view.hpp"
#include "scimate/picture.hpp"
#include "scimate/format_constants.hpp"
#include "scimate/output.hpp"
#include "scimate/sci1_io.hpp"
#include "later_game.hpp"
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <array>
#include <mutex>

using namespace desktop_detail;

MainWindow::MainWindow() {
    setWindowTitle("SCIMate NG");
    resize(1150, 760);
    auto* file = menuBar()->addMenu("&File");
    new_ = file->addAction("&New Project from Game…", this, &MainWindow::newProjectDialog);
    new_->setShortcut(QKeySequence::New);
    open_ = file->addAction("&Open Project…", this, [this] {
        const auto filename = QFileDialog::getOpenFileName(this, "Open project or exported workspace",
            dialogDirectory("projects"), "SCIMate projects and exports (project.scimate manifest.tsv)");
        if (!filename.isEmpty()) {
            rememberDirectory("projects", QFileInfo(filename).absolutePath());
            if (QFileInfo(filename).fileName() != "project.scimate" && QFileInfo(filename).fileName() != "manifest.tsv") {
                desktop_detail::show_error(this, "Open project", "Select project.scimate or an exported workspace's manifest.tsv.", scimate::LogLevel::warning);
                return;
            }
            openProject(QFileInfo(filename).absolutePath());
        }
    });
    open_->setShortcut(QKeySequence::Open);
    auto* openExport = file->addAction("Open Export Workspace…", this, [this] {
        if (busy_) return;
        const auto directory = QFileDialog::getExistingDirectory(this, "Open exported workspace", dialogDirectory("projects"));
        if (!directory.isEmpty()) openProject(directory);
    });
    openExport->setObjectName("openExportWorkspace");
    connect(open_, &QAction::changed, openExport, [this, openExport] { openExport->setEnabled(open_->isEnabled()); });
    recent_ = file->addMenu("Recent Projects");
    close_ = file->addAction("Close Project", this, [this] {
        project_.reset(); model_->removeRows(0, model_->rowCount()); preview();
        explanation_->resetAnalysis(); explanationReady_ = false; explain_->setEnabled(false); gameInfo_->setEnabled(false);
        heading_->setText("SCIMate NG"); heading_->setToolTip({}); location_->setText("Create a project from an SCI0 game, or open an existing project.");
        setWindowTitle("SCIMate NG");
        refresh_->setEnabled(false); build_->setEnabled(false); folder_->setEnabled(false); close_->setEnabled(false); rename_->setEnabled(false);
    });
    file->addSeparator();
    auto* preferencesAction = file->addAction("Preferences…", this, [this] {
        if (preferences::showDialog(this) && !busy_ && play_->isEnabled())
            play_->setChecked(preferences::animateViews());
    });
    preferencesAction->setObjectName("preferences");
    preferencesAction->setMenuRole(QAction::PreferencesRole);
    preferencesAction->setShortcut(QKeySequence::Preferences);
    file->addAction("Quit", this, &QWidget::close)->setShortcut(QKeySequence::Quit);
    auto* projectMenu = menuBar()->addMenu("&Project");
    auto* inspectLater = file->addAction("Inspect SCI1 / SCI1.1 Game…", this, [this] {
        if (busy_) return;
        auto* dialog = new LaterGameDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->show();
    });
    connect(open_, &QAction::changed, inspectLater, [this, inspectLater] { inspectLater->setEnabled(open_->isEnabled()); });
    vgaEdits_ = projectMenu->addAction("Preview VGA Workspace Edits");
    vgaEdits_->setObjectName("vgaWorkspaceEdits"); vgaEdits_->setCheckable(true);
    vgaEdits_->setChecked(QSettings().value("vgaWorkspacePreview", false).toBool());
    vgaEdits_->setToolTip("Load RGB picture and view PNG edits for an SCI1 VGA target; the source snapshots remain SCI0.");
    connect(open_, &QAction::changed, vgaEdits_, [this] { vgaEdits_->setEnabled(open_->isEnabled()); });
    connect(vgaEdits_, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue("vgaWorkspacePreview", enabled);
        if (project_ && !busy_) openProject(text(project_->directory));
    });
    gameInfo_ = projectMenu->addAction("Game Info…", this, [this] {
        if (!project_ || busy_) return;
        GameInfoDialog dialog(*project_, this);
        dialog.exec();
    });
    gameInfo_->setObjectName("gameInfo");
    auto* roomsAction = projectMenu->addAction("Room Explorer…", this, [this] {
        if (!project_ || busy_) return;
        const auto snapshot = project_;
        auto* dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle("Room Explorer — " + QString::fromStdString(snapshot->name));
        dialog->resize(1100, 760);
        auto* layout = new QVBoxLayout(dialog);
        layout->addWidget(new RoomExplorerWidget(snapshot, [this, snapshot](scimate::ResourceId target) {
            if (project_ != snapshot || busy_) return;
            filter_->clear(); types_->setCurrentIndex(0);
            for (std::size_t i = 0; i < project_->game.resources.size(); ++i) {
                const auto id = project_->game.resources[i].entry.id;
                if (id.type == target.type && id.number == target.number) {
                    browserTabs_->setCurrentIndex(0);
                    const auto row = proxy_->mapFromSource(model_->index(static_cast<int>(i), 0));
                    table_->selectRow(row.row()); table_->scrollTo(row); preview();
                    raise(); activateWindow(); return;
                }
            }
            statusBar()->showMessage("Referenced resource is absent from this project.", 8000);
        }, dialog));
        dialog->show();
    });
    roomsAction->setObjectName("roomExplorerAction");
    roomsAction->setEnabled(false);
    connect(gameInfo_, &QAction::changed, roomsAction, [this, roomsAction] { roomsAction->setEnabled(gameInfo_->isEnabled()); });
    explain_ = projectMenu->addAction("Explain Project", this, [this] {
        if (!project_ || busy_) return;
        const auto project = project_;
        run("Explaining project", [project](const scimate::ProgressCallback&) { return project; }, true);
    });
    explain_->setObjectName("explainProject");
    rename_ = projectMenu->addAction("Rename Project…", this, [this] {
        if (!project_ || busy_) return;
        bool accepted = false;
        const auto name = QInputDialog::getText(this, "Rename Project", "Project name", QLineEdit::Normal,
                                               QString::fromStdString(project_->name), &accepted);
        if (accepted) {
            try { renameProject(name); }
            catch (...) { desktop_detail::show_exception(this, "Project not renamed", scimate::LogLevel::warning); }
        }
    });
    refresh_ = projectMenu->addAction("Refresh from Disk", this, [this] { openProject(text(project_->directory)); });
    refresh_->setShortcut(QKeySequence::Refresh);
    folder_ = projectMenu->addAction("Open Resource Folder", this, &MainWindow::showFolder);
    build_ = projectMenu->addAction("Build Resources to New Folder…", this, &MainWindow::buildProjectDialog);
    build_->setShortcut(QKeySequence("Ctrl+B"));
    auto* help = menuBar()->addMenu("&Help");
    help->addAction("Project Workflow", this, [this] {
        QMessageBox::information(this, "SCIMate project workflow",
            "1. Create a project from a game containing resource.map and resource volumes.\n"
            "2. Browse resources. Open Resource Folder to edit view PNGs externally.\n"
            "3. Refresh from Disk to validate and preview your edits.\n"
            "4. Build Resources to New Folder to reconstruct and verify the map and volumes.\n\n"
            "Project files are saved on creation. The workspace can be moved; it contains its own original snapshots. "
            "Build reads the latest files from disk. Interpreters, patches and other game files are not copied.\n\n"
            "Source decompilation uses separately installed sci-tools/Snuffer. PNG editing is external; script compilation is not implemented.");
    });
    buildInterface();
}

void MainWindow::installExplanation(const scimate::GameExplanation& analysis) {
    explanation_->setAnalysis(analysis);
    explanationReady_ = true;
    if (referenceFocus_) {
        explanation_->focusResource(*referenceFocus_);
        referenceFocus_.reset();
    }
    browserTabs_->setCurrentIndex(1);
    previewExplanation();
}

void MainWindow::showResourceReferences() {
    if (!project_ || busy_) return;
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (!selected.isValid()) return;
    const auto id = project_->game.resources.at(static_cast<std::size_t>(selected.row())).entry.id;
    if (explanationReady_) {
        explanation_->focusResource(id);
        browserTabs_->setCurrentIndex(1);
        previewExplanation();
    } else {
        referenceFocus_ = id;
        const auto project = project_;
        run("Finding resource references", [project](const scimate::ProgressCallback&) { return project; }, true);
    }
}

void MainWindow::previewExplanation() {
    const auto selected = explanation_->selection();
    if (project_ && selected.resource) {
        for (std::size_t i = 0; i < project_->game.resources.size(); ++i) {
            const auto id = project_->game.resources[i].entry.id;
            if (id.type != selected.resource->type || id.number != selected.resource->number) continue;
            filter_->clear(); types_->setCurrentIndex(0);
            const auto row = proxy_->mapFromSource(model_->index(static_cast<int>(i), 0));
            table_->selectRow(row.row()); table_->scrollTo(row);
            preview();
            if (selected.loop) {
                for (int cel = 0; cel < cels_->count(); ++cel) {
                    if (cels_->itemData(cel).toPoint().x() == *selected.loop) {
                        cels_->setCurrentIndex(cel); break;
                    }
                }
            }
            details_->setPlainText(selected.evidence + "\n" + details_->toPlainText());
            if (selected.offset && selected.resource->type == scimate::ResourceType::script) {
                const auto address = QString("%1").arg(*selected.offset, 4, 16, QChar('0'));
                const QRegularExpression position("^(?:\\s*" + address + "\\b|\\.(?:class|instance) object_" + address + "\\b)",
                    QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
                const auto cursor = details_->document()->find(position);
                if (!cursor.isNull()) { details_->setTextCursor(cursor); details_->centerCursor(); }
            }
            return;
        }
    }
    play_->setChecked(false); play_->hide(); cels_->hide(); editText_->hide(); previewStack_->hide(); export_->setEnabled(false); references_->setEnabled(false);
    previewTitle_->setText("Explanation evidence");
    auto evidence = selected.evidence;
    if (selected.resource)
        evidence += "\nReferenced " + QString::fromUtf8(scimate::resource_type_name(selected.resource->type).data()) +
                    " " + QString::number(selected.resource->number) + " is absent from the loaded project.";
    if (evidence.isEmpty()) evidence = "Click Explain / Refresh to analyze the loaded project.\nThis is static analysis; runtime behavior is not proven.";
    details_->setPlainText(evidence);
}

void MainWindow::renameProject(const QString& value) {
    if (!project_ || busy_) throw std::runtime_error("Open a project and wait for loading to finish before renaming.");
    const auto name = value.trimmed().toUtf8().toStdString();
    const auto descriptor = scimate::prepare_project_rename(*project_, name);
    QSaveFile output(text(project_->directory / "project.scimate"));
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(reinterpret_cast<const char*>(descriptor.data()), static_cast<qint64>(descriptor.size())) != static_cast<qint64>(descriptor.size()) || !output.commit())
        throw std::runtime_error(("Could not save project name: " + output.errorString()).toStdString());
    project_->name = name;
    heading_->setText(QString::fromStdString(name));
    heading_->setToolTip(heading_->text() + "\n" + location_->text() + "\n" + text(project_->directory));
    setWindowTitle(heading_->text() + " — SCIMate NG");
    statusBar()->showMessage("Project name saved. The project folder has not moved.", 10000);
}

void MainWindow::openProject(const QString& directory) {
    const bool vga = vgaEdits_->isChecked();
    run("Opening project", [directory, vga](const scimate::ProgressCallback& progress) { return std::make_shared<scimate::Project>(scimate::open_project(path(directory), progress, vga)); });
}
void MainWindow::createProject(const QString& source, const QString& directory, const QString& name) {
    rememberDirectory("games", source);
    const bool vga = vgaEdits_->isChecked();
    run("Creating project", [source, directory, name, vga](const scimate::ProgressCallback& progress) {
        std::filesystem::create_directories(path(directory).parent_path());
        auto project = std::make_shared<scimate::Project>(scimate::create_project(path(source), path(directory), name.toUtf8().toStdString(), progress));
        project->vga_workspace = vga;
        return project;
    });
}
void MainWindow::buildProject(const QString& destination) {
    buildProject(destination, 0);
}
void MainWindow::buildProject(const QString& destination, int target) {
    if (!project_) return;
    const auto directory = project_->directory;
    if (target < 0 || target > 2) throw std::runtime_error("Unknown build target.");
    if (busy_) return;
    migrationReport_ = target > 0 ? QDir(destination).filePath("migration-report.txt") : QString{};
    run("Building resources to " + destination, [directory, destination, target](const scimate::ProgressCallback& progress) {
        if (target == 0) scimate::build_project(directory, path(destination), progress);
        else scimate::import_sci1_game(directory, path(destination), target == 2 ? scimate::Sci1Graphics::vga : scimate::Sci1Graphics::ega, progress);
        return ProjectPtr{};
    });
}

void MainWindow::buildProjectDialog() {
    if (!project_ || busy_) return;
    QDialog dialog(this); dialog.setWindowTitle("Build resources"); dialog.resize(680, 300);
    QVBoxLayout layout(&dialog); layout.setContentsMargins(24, 20, 24, 20);
    layout.addWidget(new QLabel("Target engine and graphics"));
    QComboBox target; target.setObjectName("buildTarget");
    target.addItems({"SCI0 EGA", "SCI1 EGA — experimental", "SCI1 VGA — experimental"});
    target.setCurrentIndex(project_->vga_workspace ? 2 : 0); layout.addWidget(&target);
    auto* hint = new QLabel; hint->setWordWrap(true); layout.addWidget(hint);
    auto update = [&] { hint->setText(target.currentIndex() == 0
        ? "Rebuild SCI0 resources. PNGs must use exact EGA colours."
        : "Migrate current workspace edits to SCI1. VGA accepts RGB picture/view edits; masks retain EGA indices. Review migration-report.txt and verify gameplay in ScummVM. Output contains resources, not a complete game installation."); };
    connect(&target, &QComboBox::currentIndexChanged, &dialog, update); update();
    layout.addWidget(new QLabel("New output folder"));
    QLineEdit destination; destination.setText(QDir(dialogDirectory("builds")).filePath("built-game"));
    auto* row = new QHBoxLayout; row->addWidget(&destination, 1); auto* browse = new QPushButton("Browse…"); row->addWidget(browse); layout.addLayout(row);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        const auto selected = QFileDialog::getSaveFileName(&dialog, "New resource output folder", destination.text(), {}, nullptr, QFileDialog::DontConfirmOverwrite);
        if (!selected.isEmpty()) destination.setText(selected);
    });
    layout.addStretch(); QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Ok)->setText("Build resources"); layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (destination.text().trimmed().isEmpty() || QFileInfo::exists(destination.text())) {
            desktop_detail::show_error(&dialog, "Choose a new folder", "The output folder must be new and must not already exist.", scimate::LogLevel::warning); return;
        }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) {
        rememberDirectory("builds", QFileInfo(destination.text()).absolutePath());
        buildProject(destination.text(), target.currentIndex());
    }
}

void MainWindow::installProject(ProjectPtr project, const std::vector<bool>& modified) {
    const QSignalBlocker selectionSignals(table_->selectionModel());
    explanation_->resetAnalysis(); explanationReady_ = false; referenceFocus_.reset(); browserTabs_->setCurrentIndex(0);
    project_ = std::move(project);
    table_->setUpdatesEnabled(false);
    model_->removeRows(0, model_->rowCount()); filter_->clear(); types_->clear(); types_->addItem("All resource types", "");
    QMap<QString, int> counts;
    for (std::size_t i = 0; i < project_->game.resources.size(); ++i) {
        const auto& r = project_->game.resources[i];
        auto* index = new QStandardItem; index->setData(static_cast<int>(i), Qt::DisplayRole);
        const auto type = scimate::resource_type_name(r.entry.id.type);
        ++counts[QString::fromUtf8(type.data(), static_cast<int>(type.size()))];
        auto* number = new QStandardItem; number->setData(r.entry.id.number, Qt::DisplayRole);
        auto* bytes = new QStandardItem; bytes->setData(static_cast<int>(r.payload.size()), Qt::DisplayRole);
        model_->appendRow({index, new QStandardItem(QString::fromUtf8(type.data(), static_cast<int>(type.size()))), number, bytes,
            new QStandardItem(modified[i] ? "Modified" : "Original")});
    }
    for (auto i = counts.cbegin(); i != counts.cend(); ++i) types_->addItem(i.key() + "  (" + QString::number(i.value()) + ")", i.key());
    table_->setUpdatesEnabled(true);
    heading_->setText(QString::fromUtf8(project_->name.c_str()));
    location_->setText(QString::number(model_->rowCount()) + (project_->vga_workspace ? " resources  ·  SCI0 source / VGA edits  ·  " : " resources  ·  SCI0 EGA  ·  ") + project_->directory.filename().u8string().c_str());
    location_->setToolTip(text(project_->directory));
    heading_->setToolTip(heading_->text() + "\n" + location_->text() + "\n" + text(project_->directory));
    setWindowTitle(heading_->text() + " — SCIMate NG");
    auto recent = QSettings().value("recentProjects").toStringList(); const auto directory = text(project_->directory);
    recent.removeAll(directory); recent.prepend(directory); while (recent.size() > 8) recent.removeLast();
    QSettings().setValue("recentProjects", recent); rememberDirectory("projects", directory); updateRecents();
    table_->setColumnWidth(0, 55); table_->setColumnWidth(1, 60); table_->setColumnWidth(2, 64); table_->setColumnWidth(3, 65);
    if (proxy_->rowCount()) { table_->selectRow(0); table_->scrollToTop(); }
    preview(); // Selection changes are blocked until the replacement model is complete.
}

void MainWindow::updateRecents() {
    recent_->clear();
    for (const auto& directory : QSettings().value("recentProjects").toStringList())
        recent_->addAction(directory, this, [this, directory] { openProject(directory); });
    if (recent_->isEmpty()) { auto* empty = recent_->addAction("No recent projects"); empty->setEnabled(false); }
}

void MainWindow::newProjectDialog() {
    QDialog dialog(this); dialog.setWindowTitle("New Project from Game"); dialog.resize(760, 400);
    dialog.setMinimumWidth(600);
    QVBoxLayout layout(&dialog);
    layout.setContentsMargins(24, 20, 24, 20); layout.setSpacing(10);
    QLineEdit name, source, destination;
    name.setObjectName("newProjectName"); source.setObjectName("newProjectSource"); destination.setObjectName("newProjectDestination");
    name.setPlaceholderText("Suggested from the game folder; you can change it");
    source.setPlaceholderText("Choose a folder containing resource.map");
    for (auto* field : {&name, &source, &destination}) {
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        field->setMinimumHeight(field->sizeHint().height() + 8);
    }
    auto addLabel = [&](const QString& title, QWidget* buddy) {
        auto* label = new QLabel(title); label->setBuddy(buddy); layout.addWidget(label);
    };
    addLabel("&Game folder", &source);
    auto* sourceRow = new QHBoxLayout;
    auto* browse = new QPushButton("Browse…"); sourceRow->addWidget(&source, 1); sourceRow->addWidget(browse);
    layout.addLayout(sourceRow);
    layout.addSpacing(6);
    addLabel("&Project name", &name); layout.addWidget(&name);
    bool customName = false;
    connect(&name, &QLineEdit::textEdited, &dialog, [&] { customName = !name.text().trimmed().isEmpty(); });
    connect(&source, &QLineEdit::textChanged, &dialog, [&] {
        const auto folder = QDir::cleanPath(QDir::fromNativeSeparators(source.text().trimmed()));
        if (!customName || name.text().trimmed().isEmpty())
            name.setText(source.text().trimmed().isEmpty() ? QString{} : QFileInfo(folder).fileName());
        source.setToolTip(source.text());
    });
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        const auto value = QFileDialog::getExistingDirectory(&dialog, "Source SCI0 game",
            source.text().isEmpty() ? dialogDirectory("games") : source.text());
        if (!value.isEmpty()) { source.setText(value); source.setCursorPosition(0); rememberDirectory("games", value); }
    });
    destination.setReadOnly(true);
    layout.addSpacing(6);
    addLabel("New project folder", &destination); layout.addWidget(&destination);
    auto* hint = new QLabel("The project folder uses the location set in Preferences. Your original game stays unchanged.");
    hint->setWordWrap(true); hint->setTextFormat(Qt::PlainText); layout.addWidget(hint);
    layout.addStretch();
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); buttons.button(QDialogButtonBox::Ok)->setText("Create Project"); layout.addWidget(&buttons);
    auto update = [&] {
        auto folder = name.text().trimmed();
        folder.replace(QRegularExpression("[\\x00-\\x1f<>:\"/\\\\|?*]"), "_");
        while (folder.endsWith('.') || folder.endsWith(' ')) folder.chop(1);
        destination.setText(folder.isEmpty() ? QString{} : QDir(preferences::projectDirectory()).filePath(folder));
        destination.setCursorPosition(0);
        const bool exists = !destination.text().isEmpty() && QFileInfo::exists(destination.text());
        destination.setToolTip(exists ? "This folder already exists. Choose another project name." : destination.text());
        buttons.button(QDialogButtonBox::Ok)->setEnabled(!folder.isEmpty() && !source.text().trimmed().isEmpty() && !exists);
    };
    for (auto* field : {&name, &source}) connect(field, &QLineEdit::textChanged, &dialog, update);
    update(); browse->setFocus(); connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) createProject(source.text(), destination.text(), name.text().trimmed());
}

void MainWindow::showFolder() {
    if (!project_ || busy_) return;
    auto directory = project_->directory;
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (selected.isValid() && (browserTabs_->currentIndex() == 0 || export_->isEnabled())) {
        const auto index = static_cast<std::size_t>(selected.row());
        const auto type = project_->game.resources[index].entry.id.type;
        const auto bundle = directory / (type == scimate::ResourceType::pic ? "pictures" : type == scimate::ResourceType::view ? "views" : "assets") / stem(index).toStdString();
        std::error_code error;
        directory = std::filesystem::is_directory(bundle, error) ? bundle : directory / "payloads";
        if (error && error != std::errc::no_such_file_or_directory) {
            desktop_detail::show_error(this, "Resource folder unavailable", QString::fromStdString(error.message()), scimate::LogLevel::warning);
            return;
        }
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(text(directory)))) desktop_detail::show_error(this, "Open resource folder", "Could not open the resource folder.", scimate::LogLevel::warning);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (busy_) { statusBar()->showMessage("Wait for the current operation to finish before closing."); event->ignore(); return; }
    QSettings().setValue("windowGeometry", saveGeometry()); QMainWindow::closeEvent(event);
}

void MainWindow::exportResource() {
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (!project_ || !selected.isValid()) return;
    const auto& resource = project_->game.resources.at(static_cast<std::size_t>(selected.row()));
    const QString binaryFilter = "Decoded resource payload (*.bin)";
    QStringList filters{binaryFilter};
    if (!previewImage_.isNull()) filters << (resource.entry.id.type == scimate::ResourceType::pic ? "Selected picture plane (*.png)" : "Selected view cel (*.png)");
    if (resource.entry.id.type == scimate::ResourceType::script) filters << "Script disassembly (*.asm)";
    if (resource.entry.id.type == scimate::ResourceType::text && editText_->isVisible()) filters << "Numbered text strings (*.tsv)";
    const auto name = QString::fromStdString(std::string(scimate::resource_type_name(resource.entry.id.type))) + "-" + QString::number(resource.entry.id.number);
    QFileDialog dialog(this, "Export selected resource");
    dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setNameFilters(filters);
    dialog.setDirectory(dialogDirectory("exports"));
    dialog.setDefaultSuffix("bin"); dialog.selectFile(name);
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString& filter) {
        dialog.setDefaultSuffix(filter.contains("*.png") ? "png" : filter.contains("*.asm") ? "asm" : filter.contains("*.tsv") ? "tsv" : "bin");
    });
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    const auto selectedFilter = dialog.selectedNameFilter();
    const auto format = selectedFilter.contains("*.png") ? "png" : selectedFilter.contains("*.asm") ? "asm" : selectedFilter.contains("*.tsv") ? "tsv" : "bin";
    const auto filename = dialog.selectedFiles().first();
    rememberDirectory("exports", QFileInfo(filename).absolutePath());
    try {
        exportSelected(filename, format); statusBar()->showMessage("Exported " + filename, 10000);
    } catch (...) { desktop_detail::show_exception(this, "Export failed", scimate::LogLevel::error); }
}

void MainWindow::saveSelectedText(const QString& table) {
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (busy_ || !project_ || !selected.isValid()) throw std::runtime_error("Select a text resource first.");
    const auto record = static_cast<std::size_t>(selected.row());
    const auto edit = scimate::prepare_project_text_edit(*project_, record, table.toStdString());
    auto writes = edit.related_writes;
    writes.emplace_back(edit.destination, edit.contents);
    scimate::replace_files(writes);
    project_->game.resources.at(record).payload = edit.payload;
    model_->item(static_cast<int>(record), 3)->setData(static_cast<int>(edit.payload.size()), Qt::DisplayRole);
    model_->item(static_cast<int>(record), 4)->setText(edit.modified ? "Modified" : "Original");
    preview(); statusBar()->showMessage("Text saved to " + text(edit.destination), 10000);
}

void MainWindow::editText() {
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (!project_ || !selected.isValid()) return;
    QDialog dialog(this); dialog.setWindowTitle("Edit text resource"); dialog.resize(760, 480);
    QVBoxLayout layout(&dialog);
    auto* hint = new QLabel("Keep the index/text header and a tab between each index and string. Indices must start at zero.\n"
                           "Preserve string order: scripts refer to these indices. Use \\n, \\r, \\t, \\\\ or \\xHH escapes.\n"
                           "Save writes immediately to the project; Cancel discards this dialog's edits.");
    hint->setWordWrap(true); layout.addWidget(hint);
    auto* editor = new QPlainTextEdit; editor->setObjectName("textEditor");
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setPlainText(QString::fromStdString(scimate::export_sci0_text_table(project_->game.resources.at(selected.row()).payload)));
    layout.addWidget(editor);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try { saveSelectedText(editor->toPlainText()); dialog.accept(); }
        catch (...) { desktop_detail::show_exception(&dialog, "Text not saved", scimate::LogLevel::warning); }
    });
    dialog.exec();
}
