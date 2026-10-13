// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.hpp"
#include "scimate/script.hpp"
#include "scimate/view.hpp"
#include "scimate/picture.hpp"
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <array>

namespace {
std::filesystem::path path(const QString& text) { return std::filesystem::u8path(text.toUtf8().constData()); }
QString text(const std::filesystem::path& p) { return QString::fromUtf8(p.u8string().c_str()); }
QString stem(std::size_t index) { return QString::number(index).rightJustified(7, '0'); }
struct Outcome { std::shared_ptr<scimate::Project> project; QString error; };
}

MainWindow::MainWindow() {
    setWindowTitle("SCIMate NG");
    resize(1150, 760);
    auto* file = menuBar()->addMenu("&File");
    new_ = file->addAction("&New Project from Game…", this, &MainWindow::newProjectDialog);
    new_->setShortcut(QKeySequence::New);
    open_ = file->addAction("&Open Project…", this, [this] {
        const auto name = QFileDialog::getOpenFileName(this, "Open SCIMate project", {}, "SCIMate projects (project.scimate)");
        if (!name.isEmpty()) openProject(QFileInfo(name).absolutePath());
    });
    open_->setShortcut(QKeySequence::Open);
    recent_ = file->addMenu("Recent Projects");
    close_ = file->addAction("Close Project", this, [this] {
        project_.reset(); model_->removeRows(0, model_->rowCount()); preview();
        heading_->setText("Your SCI projects"); location_->setText("Create a project from an SCI0 game, or open an existing project.");
        setWindowTitle("SCIMate NG");
        refresh_->setEnabled(false); build_->setEnabled(false); folder_->setEnabled(false); close_->setEnabled(false);
    });
    file->addSeparator();
    file->addAction("Quit", this, &QWidget::close)->setShortcut(QKeySequence::Quit);
    auto* projectMenu = menuBar()->addMenu("&Project");
    refresh_ = projectMenu->addAction("Refresh from Disk", this, [this] { openProject(text(project_->directory)); });
    refresh_->setShortcut(QKeySequence::Refresh);
    folder_ = projectMenu->addAction("Open Resource Folder", this, &MainWindow::showFolder);
    build_ = projectMenu->addAction("Build Resources to New Folder…", this, [this] {
        const auto destination = QFileDialog::getSaveFileName(this, "New resource output folder", text(project_->directory.parent_path() / "built-game"), {}, nullptr, QFileDialog::DontConfirmOverwrite);
        if (!destination.isEmpty()) buildProject(destination);
    });
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
    auto* toolbar = addToolBar("Project"); toolbar->setMovable(false);
    toolbar->addAction(new_); toolbar->addAction(open_); toolbar->addSeparator();
    toolbar->addAction(refresh_); toolbar->addAction(folder_); toolbar->addAction(build_);
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central); layout->setContentsMargins(20, 18, 20, 16); layout->setSpacing(12);
    heading_ = new QLabel("Your SCI projects");
    auto font = heading_->font(); font.setPointSize(font.pointSize() + 7); font.setBold(true); heading_->setFont(font);
    location_ = new QLabel("Create a project from an SCI0 game, or open an existing project.");
    location_->setTextInteractionFlags(Qt::TextSelectableByMouse); location_->setWordWrap(true);
    heading_->setTextFormat(Qt::PlainText); location_->setTextFormat(Qt::PlainText);
    layout->addWidget(heading_); layout->addWidget(location_);
    filter_ = new QLineEdit; filter_->setObjectName("resourceFilter"); filter_->setPlaceholderText("Filter resources by type, number, or status…");
    filter_->setClearButtonEnabled(true); layout->addWidget(filter_);
    auto* splitter = new QSplitter;
    table_ = new QTableView; table_->setObjectName("resources");
    model_ = new QStandardItemModel(this); model_->setHorizontalHeaderLabels({"Index", "Type", "Number", "Bytes", "Status"});
    proxy_ = new QSortFilterProxyModel(this); proxy_->setSourceModel(model_); proxy_->setFilterKeyColumn(-1); proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    table_->setModel(proxy_); table_->setSortingEnabled(true); table_->sortByColumn(0, Qt::AscendingOrder);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->verticalHeader()->hide();
    table_->setAlternatingRowColors(true); table_->horizontalHeader()->setStretchLastSection(true);
    table_->setMinimumWidth(420); splitter->addWidget(table_);
    auto* inspector = new QWidget; auto* inspectorLayout = new QVBoxLayout(inspector); inspectorLayout->setContentsMargins(12, 0, 0, 0);
    cels_ = new QComboBox; cels_->setObjectName("cels"); cels_->hide(); inspectorLayout->addWidget(cels_);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true);
    image_ = new QLabel; image_->setAlignment(Qt::AlignCenter); image_->setMinimumHeight(160);
    image_->setStyleSheet("background: #33383e;"); scroll->setWidget(image_); inspectorLayout->addWidget(scroll, 1);
    details_ = new QPlainTextEdit; details_->setObjectName("details"); details_->setReadOnly(true);
    details_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    details_->setPlainText("Select a resource to inspect it.\n\nViews show decoded cels, including mirrored loops.\nScripts show bytecode disassembly.\nOther resources show a bounded hexadecimal preview.");
    inspectorLayout->addWidget(details_, 2); splitter->addWidget(inspector); splitter->setStretchFactor(1, 1); layout->addWidget(splitter, 1);
    auto* note = new QLabel("SCI0 • Edit PNGs in an external editor, then refresh and build. Script compilation is not available yet.");
    note->setWordWrap(true); layout->addWidget(note); setCentralWidget(central);
    progress_ = new QProgressBar; progress_->setRange(0, 0); progress_->setMaximumWidth(160); progress_->hide(); statusBar()->addPermanentWidget(progress_);
    connect(filter_, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { preview(); });
    connect(cels_, &QComboBox::currentIndexChanged, this, [this] { previewCel(); });
    for (auto* action : {refresh_, folder_, build_, close_}) action->setEnabled(false);
    updateRecents();
    restoreGeometry(QSettings().value("windowGeometry").toByteArray());
}

void MainWindow::run(const QString& message, std::function<ProjectPtr()> operation) {
    if (busy_) return;
    busy_ = true;
    for (auto* action : {new_, open_, refresh_, folder_, build_, close_}) action->setEnabled(false);
    recent_->setEnabled(false); progress_->show(); statusBar()->showMessage(message);
    auto* watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, message] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false; progress_->hide();
        new_->setEnabled(true); open_->setEnabled(true); recent_->setEnabled(true);
        if (result.error.isEmpty()) {
            if (result.project) installProject(result.project);
            statusBar()->showMessage(message + " — done", 10000);
        } else {
            statusBar()->showMessage("Operation failed; previous project remains open.");
            QMessageBox::critical(this, "SCIMate", result.error);
        }
        for (auto* action : {refresh_, folder_, build_, close_}) action->setEnabled(bool(project_));
    });
    watcher->setFuture(QtConcurrent::run([operation = std::move(operation)] {
        try { return Outcome{operation(), {}}; }
        catch (const std::exception& error) { return Outcome{{}, QString::fromUtf8(error.what())}; }
    }));
}

void MainWindow::openProject(const QString& directory) {
    run("Opening project", [directory] { return std::make_shared<scimate::Project>(scimate::open_project(path(directory))); });
}
void MainWindow::createProject(const QString& source, const QString& directory, const QString& name) {
    run("Creating project", [source, directory, name] { return std::make_shared<scimate::Project>(scimate::create_project(path(source), path(directory), name.toUtf8().toStdString())); });
}
void MainWindow::buildProject(const QString& destination) {
    if (!project_) return;
    const auto directory = project_->directory;
    run("Building resources to " + destination, [directory, destination] { scimate::build_project(directory, path(destination)); return ProjectPtr{}; });
}

void MainWindow::installProject(ProjectPtr project) {
    project_ = std::move(project);
    const auto original = scimate::read_sci0_game(project_->game.original);
    model_->removeRows(0, model_->rowCount()); filter_->clear();
    for (std::size_t i = 0; i < project_->game.resources.size(); ++i) {
        const auto& r = project_->game.resources[i];
        auto* index = new QStandardItem; index->setData(static_cast<int>(i), Qt::DisplayRole);
        const auto type = scimate::resource_type_name(r.entry.id.type);
        auto* number = new QStandardItem; number->setData(r.entry.id.number, Qt::DisplayRole);
        auto* bytes = new QStandardItem; bytes->setData(static_cast<int>(r.payload.size()), Qt::DisplayRole);
        model_->appendRow({index, new QStandardItem(QString::fromUtf8(type.data(), static_cast<int>(type.size()))), number, bytes,
            new QStandardItem(r.payload == original.resources[i].payload ? "Original" : "Modified")});
    }
    heading_->setText(QString::fromUtf8(project_->name.c_str()));
    location_->setText(text(project_->directory) + "\n" + QString::number(model_->rowCount()) + " resources • SCI0");
    setWindowTitle(heading_->text() + " — SCIMate NG");
    auto recent = QSettings().value("recentProjects").toStringList(); const auto directory = text(project_->directory);
    recent.removeAll(directory); recent.prepend(directory); while (recent.size() > 8) recent.removeLast();
    QSettings().setValue("recentProjects", recent); updateRecents(); table_->resizeColumnsToContents();
    preview(); if (proxy_->rowCount()) { table_->selectRow(0); table_->scrollToTop(); }
}

void MainWindow::updateRecents() {
    recent_->clear();
    for (const auto& directory : QSettings().value("recentProjects").toStringList())
        recent_->addAction(directory, this, [this, directory] { openProject(directory); });
    if (recent_->isEmpty()) { auto* empty = recent_->addAction("No recent projects"); empty->setEnabled(false); }
}

void MainWindow::newProjectDialog() {
    QDialog dialog(this); dialog.setWindowTitle("New Project from Game"); dialog.resize(620, 220);
    QFormLayout layout(&dialog);
    QLineEdit name("My SCI project"), source, destination;
    layout.addRow("Project name", &name);
    auto addPath = [&](const QString& label, QLineEdit* field, bool newFolder) {
        auto* row = new QWidget; auto* rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* browse = new QPushButton("Browse…"); rowLayout->addWidget(field); rowLayout->addWidget(browse); layout.addRow(label, row);
        connect(browse, &QPushButton::clicked, &dialog, [&, field, newFolder] {
            const auto value = newFolder ? QFileDialog::getSaveFileName(&dialog, "New project folder", field->text(), {}, nullptr, QFileDialog::DontConfirmOverwrite)
                                         : QFileDialog::getExistingDirectory(&dialog, "Source SCI0 game", field->text());
            if (!value.isEmpty()) field->setText(value);
        });
    };
    addPath("Game folder", &source, false); addPath("New project folder", &destination, true);
    auto* hint = new QLabel("Choose a project folder that does not exist yet. The game stays unchanged.\nThe project stores original resources and editable PNG/payload files.");
    hint->setWordWrap(true); layout.addRow(hint);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); buttons.button(QDialogButtonBox::Ok)->setText("Create Project"); layout.addRow(&buttons);
    auto update = [&] { buttons.button(QDialogButtonBox::Ok)->setEnabled(!name.text().trimmed().isEmpty() && !source.text().isEmpty() && !destination.text().isEmpty()); };
    for (auto* field : {&name, &source, &destination}) connect(field, &QLineEdit::textChanged, &dialog, update);
    update(); connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) createProject(source.text(), destination.text(), name.text().trimmed());
}

void MainWindow::preview() {
    selectedView_.clear(); selectedPicture_.clear(); cels_->clear(); cels_->hide(); image_->clear(); details_->clear();
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (!project_ || !selected.isValid()) return;
    const auto index = static_cast<std::size_t>(selected.row());
    const auto& resource = project_->game.resources.at(index);
    QString description = "Record " + QString::number(index) + " • " + QString::fromUtf8(scimate::resource_type_name(resource.entry.id.type).data()) + " " + QString::number(resource.entry.id.number) +
        "\nPayload: payloads/" + stem(index) + ".bin\n";
    try {
        if (resource.entry.id.type == scimate::ResourceType::view) {
            const auto view = scimate::read_sci0_view(resource.payload); selectedView_ = resource.payload;
            description += "Editable folder: views/" + stem(index) + "\nPreview reflects the last successful refresh.\n";
            for (std::size_t loop = 0; loop < view.loops.size(); ++loop) {
                const auto& entry = view.loops[loop]; const auto& source = entry.mirror_of < 0 ? entry : view.loops.at(static_cast<std::size_t>(entry.mirror_of));
                for (std::size_t cel = 0; cel < source.cels.size(); ++cel)
                    cels_->addItem(QString("Loop %1 / Cel %2%3").arg(loop).arg(cel).arg(entry.mirror_of < 0 ? "" : " (mirrored)"), QPoint(static_cast<int>(loop), static_cast<int>(cel)));
            }
            cels_->setVisible(cels_->count() > 0); previewCel();
        } else if (resource.entry.id.type == scimate::ResourceType::pic) {
            scimate::read_sci0_picture(resource.payload);
            selectedPicture_ = resource.payload;
            description += "Editable folder: pictures/" + stem(index) + "\nMask colors represent EGA indices 0–15.\nPreview reflects the last successful refresh.\n";
            cels_->addItems({"Visual background", "Priority mask", "Control mask"});
            cels_->show(); previewCel();
        } else if (resource.entry.id.type == scimate::ResourceType::script) {
            description += "\n" + QString::fromStdString(scimate::disassemble_sci0_script(resource.payload, resource.entry.id.number));
        } else {
            description += "\nHexadecimal preview (first 512 bytes):\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(resource.payload.size(), 512); ++i) {
                if (i % 16 == 0) description += QString("\n%1  ").arg(i, 4, 16, QChar('0'));
                description += QString("%1 ").arg(resource.payload[i], 2, 16, QChar('0'));
            }
        }
    } catch (const std::exception& error) { description += "\nPreview unavailable: " + QString::fromUtf8(error.what()); }
    details_->setPlainText(description);
}

void MainWindow::previewCel() {
    constexpr std::array<unsigned, 16> palette{0x000000,0x0000aa,0x00aa00,0x00aaaa,0xaa0000,0xaa00aa,0xaa5500,0xaaaaaa,0x555555,0x5555ff,0x55ff55,0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff};
    if (!selectedPicture_.empty() && cels_->currentIndex() >= 0) {
        const auto picture = scimate::read_sci0_picture(selectedPicture_);
        const auto& pixels = picture.planes.at(static_cast<std::size_t>(cels_->currentIndex()));
        QImage image(320, 190, QImage::Format_RGB32);
        for (int y = 0; y < 190; ++y) for (int x = 0; x < 320; ++x)
            image.setPixel(x, y, 0xff000000u | palette[pixels[static_cast<std::size_t>(y) * 320 + x]]);
        image_->setPixmap(QPixmap::fromImage(image.scaled(640, 380, Qt::KeepAspectRatio, Qt::FastTransformation)));
        image_->setToolTip("320 × 190 • " + cels_->currentText());
        return;
    }
    if (selectedView_.empty() || cels_->currentIndex() < 0) return;
    const auto view = scimate::read_sci0_view(selectedView_); const auto position = cels_->currentData().toPoint();
    const auto& loop = view.loops.at(static_cast<std::size_t>(position.x()));
    const auto& source = loop.mirror_of < 0 ? loop : view.loops.at(static_cast<std::size_t>(loop.mirror_of));
    const auto& cel = source.cels.at(static_cast<std::size_t>(position.y()));
    QImage image(cel.width, cel.height, QImage::Format_ARGB32);
    for (int y = 0; y < cel.height; ++y) for (int x = 0; x < cel.width; ++x) {
        const auto color = cel.pixels[static_cast<std::size_t>(y) * cel.width + x];
        image.setPixel(loop.mirror_of < 0 ? x : cel.width - x - 1, y, color == cel.transparent ? 0 : 0xff000000u | palette[color]);
    }
    image_->setPixmap(QPixmap::fromImage(image.scaled(cel.width * 2, cel.height * 2, Qt::KeepAspectRatio, Qt::FastTransformation)));
    image_->setToolTip(QString("%1 × %2 • placement (%3, %4)").arg(cel.width).arg(cel.height).arg(cel.x).arg(cel.y));
}

void MainWindow::showFolder() {
    auto directory = project_->directory;
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (selected.isValid()) {
        const auto index = static_cast<std::size_t>(selected.row());
        const auto type = project_->game.resources[index].entry.id.type;
        const auto bundle = directory / (type == scimate::ResourceType::pic ? "pictures" : "views") / stem(index).toStdString();
        directory = (type == scimate::ResourceType::view || type == scimate::ResourceType::pic) && std::filesystem::is_directory(bundle) ? bundle : directory / "payloads";
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(text(directory)))) QMessageBox::warning(this, "SCIMate", "Could not open the resource folder.");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (busy_) { statusBar()->showMessage("Wait for the current operation to finish before closing."); event->ignore(); return; }
    QSettings().setValue("windowGeometry", saveGeometry()); QMainWindow::closeEvent(event);
}
