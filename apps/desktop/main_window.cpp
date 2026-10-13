// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.hpp"
#include "scimate/script.hpp"
#include "scimate/view.hpp"
#include "scimate/picture.hpp"
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <array>
#include <mutex>

namespace {
std::filesystem::path path(const QString& text) { return std::filesystem::u8path(text.toUtf8().constData()); }
QString text(const std::filesystem::path& p) { return QString::fromUtf8(p.u8string().c_str()); }
QString stem(std::size_t index) { return QString::number(index).rightJustified(7, '0'); }
struct Outcome { std::shared_ptr<scimate::Project> project; QString error; std::vector<bool> modified; };
struct ProgressState { std::mutex mutex; scimate::OperationProgress value; };
class ResourceFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setType(const QString& type) { type_ = type; invalidate(); }
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        return (type_.isEmpty() || sourceModel()->index(row, 1, parent).data().toString() == type_)
            && QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }
private:
    QString type_;
};
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
    setMinimumSize(900, 620);
    // Keep inputs, menus, buttons, table selection and progress indicators native.
    // Only the surrounding layout panels receive lightweight decoration.
    const auto colors = palette();
    setStyleSheet(QString(R"(
        QLabel#brand { color: %1; font-size: 11px; font-weight: 600; }
        QLabel#heading { font-size: 23px; font-weight: 600; }
        QLabel#location { color: %2; font-size: 12px; }
        QLabel#section { font-size: 13px; font-weight: 600; }
        QFrame#card { background: %3; border: 1px solid %4; border-radius: 8px; }
        QWidget#progressPanel { background: %3; border: 1px solid %4; border-radius: 8px; }
        QSplitter::handle { background: %4; }
    )").arg(colors.color(QPalette::Highlight).name(),
             colors.color(QPalette::PlaceholderText).name(),
             colors.color(QPalette::Base).name(), colors.color(QPalette::Mid).name()));
    auto actionButton = [](QAction* action, const QString& label, bool primary = false) {
        auto* button = new QPushButton(label);
        if (primary) { button->setObjectName("primary"); button->setDefault(true); }
        button->setToolTip(action->text()); button->setEnabled(action->isEnabled());
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, button, [button, action] { button->setEnabled(action->isEnabled()); });
        return button;
    };
    auto* central = new QWidget; central->setObjectName("content");
    auto* layout = new QVBoxLayout(central); layout->setContentsMargins(24, 22, 24, 16); layout->setSpacing(16);
    auto* header = new QHBoxLayout;
    auto* title = new QVBoxLayout; title->setSpacing(5);
    auto* brand = new QLabel("SCIMATE  /  WORKSPACE"); brand->setObjectName("brand"); title->addWidget(brand);
    heading_ = new QLabel("Your next adventure"); heading_->setObjectName("heading");
    location_ = new QLabel("Create a project from an SCI0 game to start exploring."); location_->setObjectName("location");
    location_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    heading_->setTextFormat(Qt::PlainText); location_->setTextFormat(Qt::PlainText);
    title->addWidget(heading_); title->addWidget(location_); header->addLayout(title, 1);
    header->addWidget(actionButton(new_, "New project")); header->addWidget(actionButton(open_, "Open…"));
    header->addWidget(actionButton(build_, "Build resources…", true)); layout->addLayout(header);
    progressPanel_ = new QWidget; progressPanel_->setObjectName("progressPanel");
    auto* progressLayout = new QVBoxLayout(progressPanel_); progressLayout->setContentsMargins(16, 12, 16, 12); progressLayout->setSpacing(6);
    progressTitle_ = new QLabel; progressTitle_->setObjectName("section"); progressTitle_->setTextFormat(Qt::PlainText);
    progressDetail_ = new QLabel; progressDetail_->setObjectName("progressDetail"); progressDetail_->setTextFormat(Qt::PlainText);
    progressDetail_->setWordWrap(true);
    progress_ = new QProgressBar; progress_->setObjectName("operationProgress"); progress_->setTextVisible(false); progress_->setFixedHeight(14);
    progressLayout->addWidget(progressTitle_); progressLayout->addWidget(progressDetail_); progressLayout->addWidget(progress_);
    layout->addWidget(progressPanel_); progressPanel_->hide();
    progressTimer_ = new QTimer(this); progressTimer_->setInterval(100);
    auto* splitter = new QSplitter; splitter->setObjectName("browserSplitter"); splitter->setHandleWidth(7); splitter->setChildrenCollapsible(false);
    auto* browser = new QFrame; browser->setObjectName("card"); auto* browserLayout = new QVBoxLayout(browser); browserLayout->setContentsMargins(14, 14, 14, 8); browserLayout->setSpacing(10);
    auto* browserHeader = new QHBoxLayout; auto* resourceTitle = new QLabel("Resources"); resourceTitle->setObjectName("section"); browserHeader->addWidget(resourceTitle); browserHeader->addStretch();
    browserHeader->addWidget(actionButton(refresh_, "Refresh")); browserLayout->addLayout(browserHeader);
    types_ = new QComboBox; types_->setObjectName("resourceType"); types_->addItem("All resource types", ""); browserLayout->addWidget(types_);
    filter_ = new QLineEdit; filter_->setObjectName("resourceFilter"); filter_->setPlaceholderText("Search number, type or status…"); filter_->setClearButtonEnabled(true); browserLayout->addWidget(filter_);
    table_ = new QTableView; table_->setObjectName("resources");
    model_ = new QStandardItemModel(this); model_->setHorizontalHeaderLabels({"Entry", "Type", "Number", "Bytes", "Status"});
    auto* resourceFilter = new ResourceFilter(this); proxy_ = resourceFilter; proxy_->setSourceModel(model_); proxy_->setFilterKeyColumn(-1); proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(types_, &QComboBox::currentIndexChanged, this, [this, resourceFilter] { resourceFilter->setType(types_->currentData().toString()); });
    table_->setModel(proxy_); table_->setSortingEnabled(true); table_->sortByColumn(0, Qt::AscendingOrder);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->verticalHeader()->hide(); table_->verticalHeader()->setDefaultSectionSize(32);
    table_->setShowGrid(false); table_->setAlternatingRowColors(true); table_->horizontalHeader()->setStretchLastSection(true);
    browser->setMinimumWidth(345); browserLayout->addWidget(table_, 1); splitter->addWidget(browser);
    auto* inspector = new QFrame; inspector->setObjectName("card"); auto* inspectorLayout = new QVBoxLayout(inspector); inspectorLayout->setContentsMargins(14, 14, 14, 14); inspectorLayout->setSpacing(10);
    auto* inspectorHeader = new QHBoxLayout; auto* previewTitle = new QLabel("Resource preview"); previewTitle->setObjectName("section"); inspectorHeader->addWidget(previewTitle); inspectorHeader->addStretch();
    inspectorHeader->addWidget(actionButton(folder_, "Open folder"));
    export_ = new QPushButton("Export…"); export_->setObjectName("exportResource"); export_->setEnabled(false); inspectorHeader->addWidget(export_);
    connect(export_, &QPushButton::clicked, this, &MainWindow::exportResource); inspectorLayout->addLayout(inspectorHeader);
    auto* controls = new QHBoxLayout;
    cels_ = new QComboBox; cels_->setObjectName("cels"); cels_->hide(); controls->addWidget(cels_, 1);
    play_ = new QPushButton("Play loop"); play_->setObjectName("playLoop"); play_->setCheckable(true); play_->setEnabled(false); play_->setToolTip("Preview the selected loop at about 8 frames per second"); controls->addWidget(play_);
    playTimer_ = new QTimer(this); playTimer_->setInterval(120);
    connect(playTimer_, &QTimer::timeout, this, &MainWindow::advanceCel);
    connect(play_, &QPushButton::toggled, this, [this](bool active) { play_->setText(active ? "Pause" : "Play loop"); if (active) playTimer_->start(); else playTimer_->stop(); });
    inspectorLayout->addLayout(controls);
    auto* previewSplit = new QSplitter(Qt::Vertical); previewSplit->setObjectName("previewSplitter"); previewSplit->setHandleWidth(7); previewSplit->setChildrenCollapsible(false);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setMinimumHeight(80);
    image_ = new QLabel("Select a resource to preview"); image_->setAlignment(Qt::AlignCenter); image_->setMinimumHeight(60);
    image_->setStyleSheet("background: #202c38; color: #a9b8c7; padding: 12px;"); scroll->setWidget(image_); previewSplit->addWidget(scroll);
    details_ = new QPlainTextEdit; details_->setObjectName("details"); details_->setReadOnly(true); details_->setMinimumHeight(80);
    details_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); details_->setPlaceholderText("Resource details, script disassembly and binary previews appear here.");
    previewSplit->addWidget(details_); previewSplit->setStretchFactor(0, 3); previewSplit->setStretchFactor(1, 1); previewSplit->setSizes({420, 170});
    inspectorLayout->addWidget(previewSplit, 1); splitter->addWidget(inspector); splitter->setStretchFactor(0, 2); splitter->setStretchFactor(1, 3); splitter->setSizes({420, 650}); layout->addWidget(splitter, 1);
    auto* note = new QLabel("Edit exported PNGs in your image editor, refresh to review, then build to a new folder."); note->setObjectName("location"); note->setWordWrap(true); layout->addWidget(note);
    setCentralWidget(central);
    connect(filter_, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { preview(); });
    connect(cels_, &QComboBox::currentIndexChanged, this, [this] { previewCel(); });
    for (auto* action : {refresh_, folder_, build_, close_}) action->setEnabled(false);
    updateRecents();
    restoreGeometry(QSettings().value("windowGeometry").toByteArray());
}

void MainWindow::run(const QString& message, std::function<ProjectPtr(const scimate::ProgressCallback&)> operation) {
    if (busy_) return;
    busy_ = true;
    for (auto* action : {new_, open_, refresh_, folder_, build_, close_}) action->setEnabled(false);
    recent_->setEnabled(false); progressPanel_->show(); progress_->setRange(0, 0); progressTitle_->setText(message);
    progressDetail_->setText("Starting…"); statusBar()->showMessage(message); elapsed_.start();
    auto state = std::make_shared<ProgressState>();
    const scimate::ProgressCallback progress = [state](const scimate::OperationProgress& value) { std::lock_guard<std::mutex> lock(state->mutex); state->value = value; };
    progressTimer_->disconnect(this);
    connect(progressTimer_, &QTimer::timeout, this, [this, state] {
        scimate::OperationProgress current;
        { std::lock_guard<std::mutex> lock(state->mutex); current = state->value; }
        if (!current.phase.empty()) progressTitle_->setText(QString::fromStdString(current.phase));
        const auto elapsed = elapsed_.elapsed() / 1000;
        QString detail = QString::fromStdString(current.detail);
        if (current.total) {
            progress_->setRange(0, static_cast<int>(current.total)); progress_->setValue(static_cast<int>(current.completed));
            detail += QString("  •  %1 / %2 resources").arg(current.completed).arg(current.total);
        } else progress_->setRange(0, 0);
        progressDetail_->setText(detail + QString("  •  %1m %2s elapsed").arg(elapsed / 60).arg(elapsed % 60));
    });
    progressTimer_->start();
    auto* watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, message] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false; progressTimer_->stop(); progressPanel_->hide();
        new_->setEnabled(true); open_->setEnabled(true); recent_->setEnabled(true);
        if (result.error.isEmpty()) {
            if (result.project) installProject(result.project, result.modified);
            statusBar()->showMessage(message + QString(" — done in %1 s").arg(elapsed_.elapsed() / 1000.0, 0, 'f', 1), 10000);
        } else {
            statusBar()->showMessage("Operation failed; previous project remains open.");
            QMessageBox::critical(this, "SCIMate", result.error);
        }
        for (auto* action : {refresh_, folder_, build_, close_}) action->setEnabled(bool(project_));
    });
    watcher->setFuture(QtConcurrent::run([operation = std::move(operation), progress] {
        try {
            auto project = operation(progress);
            std::vector<bool> modified;
            if (project) {
                progress({"Preparing resource browser", 0, 0, "Comparing current resources with originals"});
                const auto original = scimate::read_sci0_game(project->game.original);
                for (std::size_t i = 0; i < project->game.resources.size(); ++i)
                    modified.push_back(project->game.resources[i].payload != original.resources[i].payload);
            }
            return Outcome{std::move(project), {}, std::move(modified)};
        }
        catch (const std::exception& error) { return Outcome{{}, QString::fromUtf8(error.what()), {}}; }
    }));
}

void MainWindow::openProject(const QString& directory) {
    run("Opening project", [directory](const scimate::ProgressCallback& progress) { return std::make_shared<scimate::Project>(scimate::open_project(path(directory), progress)); });
}
void MainWindow::createProject(const QString& source, const QString& directory, const QString& name) {
    run("Creating project", [source, directory, name](const scimate::ProgressCallback& progress) { return std::make_shared<scimate::Project>(scimate::create_project(path(source), path(directory), name.toUtf8().toStdString(), progress)); });
}
void MainWindow::buildProject(const QString& destination) {
    if (!project_) return;
    const auto directory = project_->directory;
    run("Building resources to " + destination, [directory, destination](const scimate::ProgressCallback& progress) { scimate::build_project(directory, path(destination), progress); return ProjectPtr{}; });
}

void MainWindow::installProject(ProjectPtr project, const std::vector<bool>& modified) {
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
    location_->setText(QString::number(model_->rowCount()) + " resources  ·  SCI0  ·  " + project_->directory.filename().u8string().c_str());
    location_->setToolTip(text(project_->directory));
    setWindowTitle(heading_->text() + " — SCIMate NG");
    auto recent = QSettings().value("recentProjects").toStringList(); const auto directory = text(project_->directory);
    recent.removeAll(directory); recent.prepend(directory); while (recent.size() > 8) recent.removeLast();
    QSettings().setValue("recentProjects", recent); updateRecents();
    table_->setColumnWidth(0, 55); table_->setColumnWidth(1, 60); table_->setColumnWidth(2, 64); table_->setColumnWidth(3, 65);
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
    play_->setChecked(false); play_->setEnabled(false); export_->setEnabled(false);
    previewImage_ = {}; previewView_ = {}; previewPicture_ = {};
    selectedView_.clear(); selectedPicture_.clear(); cels_->clear(); cels_->hide(); image_->clear(); details_->clear();
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (!project_ || !selected.isValid()) return;
    const auto index = static_cast<std::size_t>(selected.row());
    const auto& resource = project_->game.resources.at(index);
    export_->setEnabled(true);
    QString description = "Record " + QString::number(index) + " • " + QString::fromUtf8(scimate::resource_type_name(resource.entry.id.type).data()) + " " + QString::number(resource.entry.id.number) +
        "\nPayload: payloads/" + stem(index) + ".bin\n";
    if (std::filesystem::is_directory(project_->directory / "assets" / stem(index).toStdString()))
        description += "Editable/binary bundle: assets/" + stem(index) + "\nOpen Resource Folder to edit, then Refresh from Disk.\n";
    try {
        if (resource.entry.id.type == scimate::ResourceType::view) {
            previewView_ = scimate::read_sci0_view(resource.payload); const auto& view = previewView_; selectedView_ = resource.payload;
            description += "Editable folder: views/" + stem(index) + "\nPreview reflects the last successful refresh.\n";
            for (std::size_t loop = 0; loop < view.loops.size(); ++loop) {
                const auto& entry = view.loops[loop]; const auto& source = entry.mirror_of < 0 ? entry : view.loops.at(static_cast<std::size_t>(entry.mirror_of));
                for (std::size_t cel = 0; cel < source.cels.size(); ++cel)
                    cels_->addItem(QString("Loop %1 / Cel %2%3").arg(loop).arg(cel).arg(entry.mirror_of < 0 ? "" : " (mirrored)"), QPoint(static_cast<int>(loop), static_cast<int>(cel)));
            }
            cels_->setVisible(cels_->count() > 0); play_->setEnabled(cels_->count() > 0); previewCel();
        } else if (resource.entry.id.type == scimate::ResourceType::pic) {
            previewPicture_ = scimate::read_sci0_picture(resource.payload);
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
        const auto& picture = previewPicture_;
        const auto& pixels = picture.planes.at(static_cast<std::size_t>(cels_->currentIndex()));
        QImage image(320, 190, QImage::Format_RGB32);
        for (int y = 0; y < 190; ++y) for (int x = 0; x < 320; ++x)
            image.setPixel(x, y, 0xff000000u | palette[pixels[static_cast<std::size_t>(y) * 320 + x]]);
        previewImage_ = image;
        image_->setPixmap(QPixmap::fromImage(image.scaled(640, 380, Qt::KeepAspectRatio, Qt::FastTransformation)));
        image_->setToolTip("320 × 190 • " + cels_->currentText());
        return;
    }
    if (selectedView_.empty() || cels_->currentIndex() < 0) return;
    const auto& view = previewView_; const auto position = cels_->currentData().toPoint();
    const auto& loop = view.loops.at(static_cast<std::size_t>(position.x()));
    const auto& source = loop.mirror_of < 0 ? loop : view.loops.at(static_cast<std::size_t>(loop.mirror_of));
    const auto& cel = source.cels.at(static_cast<std::size_t>(position.y()));
    QImage image(cel.width, cel.height, QImage::Format_ARGB32);
    for (int y = 0; y < cel.height; ++y) for (int x = 0; x < cel.width; ++x) {
        const auto color = cel.pixels[static_cast<std::size_t>(y) * cel.width + x];
        image.setPixel(loop.mirror_of < 0 ? x : cel.width - x - 1, y, color == cel.transparent ? 0 : 0xff000000u | palette[color]);
    }
    previewImage_ = image;
    image_->setPixmap(QPixmap::fromImage(image.scaled(cel.width * 2, cel.height * 2, Qt::KeepAspectRatio, Qt::FastTransformation)));
    image_->setToolTip(QString("%1 × %2 • placement (%3, %4)").arg(cel.width).arg(cel.height).arg(cel.x).arg(cel.y));
}

void MainWindow::showFolder() {
    auto directory = project_->directory;
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (selected.isValid()) {
        const auto index = static_cast<std::size_t>(selected.row());
        const auto type = project_->game.resources[index].entry.id.type;
        const auto bundle = directory / (type == scimate::ResourceType::pic ? "pictures" : type == scimate::ResourceType::view ? "views" : "assets") / stem(index).toStdString();
        directory = std::filesystem::is_directory(bundle) ? bundle : directory / "payloads";
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(text(directory)))) QMessageBox::warning(this, "SCIMate", "Could not open the resource folder.");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (busy_) { statusBar()->showMessage("Wait for the current operation to finish before closing."); event->ignore(); return; }
    QSettings().setValue("windowGeometry", saveGeometry()); QMainWindow::closeEvent(event);
}

void MainWindow::advanceCel() {
    if (selectedView_.empty() || cels_->currentIndex() < 0) { play_->setChecked(false); return; }
    const auto loop = cels_->currentData().toPoint().x();
    const int start = cels_->currentIndex();
    for (int step = 1; step <= cels_->count(); ++step) {
        const int next = (start + step) % cels_->count();
        if (cels_->itemData(next).toPoint().x() == loop) { cels_->setCurrentIndex(next); return; }
    }
}

void MainWindow::exportSelected(const QString& destination, const QString& format) {
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (!project_ || !selected.isValid()) throw std::runtime_error("Select a resource first.");
    const auto& resource = project_->game.resources.at(static_cast<std::size_t>(selected.row()));
    QByteArray data;
    if (format == "bin") data = QByteArray(reinterpret_cast<const char*>(resource.payload.data()), static_cast<int>(resource.payload.size()));
    else if (format == "png" && !previewImage_.isNull()) {
        QBuffer buffer(&data); buffer.open(QIODevice::WriteOnly);
        if (!previewImage_.save(&buffer, "PNG")) throw std::runtime_error("Could not encode preview PNG.");
    } else if (format == "asm" && resource.entry.id.type == scimate::ResourceType::script)
        data = QByteArray::fromStdString(scimate::disassemble_sci0_script(resource.payload, resource.entry.id.number));
    else throw std::runtime_error("This export format is not available for the selected resource.");
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit())
        throw std::runtime_error(("Could not export resource: " + output.errorString()).toStdString());
}

void MainWindow::exportResource() {
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (!project_ || !selected.isValid()) return;
    const auto& resource = project_->game.resources.at(static_cast<std::size_t>(selected.row()));
    const QString binaryFilter = "Decoded resource payload (*.bin)";
    QStringList filters{binaryFilter};
    if (!previewImage_.isNull()) filters << (resource.entry.id.type == scimate::ResourceType::pic ? "Selected picture plane (*.png)" : "Selected view cel (*.png)");
    if (resource.entry.id.type == scimate::ResourceType::script) filters << "Script disassembly (*.asm)";
    const auto name = QString::fromStdString(std::string(scimate::resource_type_name(resource.entry.id.type))) + "-" + QString::number(resource.entry.id.number);
    QFileDialog dialog(this, "Export selected resource");
    dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setNameFilters(filters);
    dialog.setDefaultSuffix("bin"); dialog.selectFile(name);
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString& filter) {
        dialog.setDefaultSuffix(filter.contains("*.png") ? "png" : filter.contains("*.asm") ? "asm" : "bin");
    });
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    const auto selectedFilter = dialog.selectedNameFilter();
    const auto format = selectedFilter.contains("*.png") ? "png" : selectedFilter.contains("*.asm") ? "asm" : "bin";
    const auto filename = dialog.selectedFiles().first();
    try {
        exportSelected(filename, format); statusBar()->showMessage("Exported " + filename, 10000);
    } catch (const std::exception& error) { QMessageBox::critical(this, "Export failed", QString::fromUtf8(error.what())); }
}
