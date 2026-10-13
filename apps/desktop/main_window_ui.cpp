// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.hpp"
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
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <array>
#include <mutex>

using namespace desktop_detail;
namespace {
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
void MainWindow::buildInterface() {
    setMinimumSize(900, 620);
    // Keep inputs, menus, buttons, table selection and progress indicators native.
    // Only the surrounding layout panels receive lightweight decoration.
    const auto colors = palette();
    setStyleSheet(QString(R"(
        QLabel#heading { color: %1; font-size: 15px; font-weight: 600; }
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
    auto* layout = new QVBoxLayout(central); layout->setContentsMargins(12, 8, 12, 8); layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    heading_ = new QLabel("SCIMate NG"); heading_->setObjectName("heading");
    heading_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    location_ = new QLabel("Create a project from an SCI0 game to start exploring.", central); location_->setObjectName("location");
    location_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    heading_->setTextFormat(Qt::PlainText); location_->setTextFormat(Qt::PlainText);
    location_->hide(); // Full project context is available on the title tooltip.
    header->addWidget(heading_, 1);
    header->addWidget(actionButton(rename_, "Rename…"));
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
    browser->setMinimumWidth(345); browserLayout->addWidget(table_, 1);
    browserTabs_ = new QTabWidget; browserTabs_->addTab(browser, "Resources");
    auto* explainPage = new QWidget; auto* explainLayout = new QVBoxLayout(explainPage);
    auto* explainButtons = new QHBoxLayout;
    explainButtons->addWidget(actionButton(explain_, "Explain / Refresh"));
    auto* collapse = new QPushButton("Collapse all"); explainButtons->addWidget(collapse);
    explainLayout->addLayout(explainButtons);
    explanation_ = new ExplanationTree; explainLayout->addWidget(explanation_, 1);
    auto* explainHint = new QLabel("Select a link to preview its resource. References shows outgoing links; Referenced by shows source scripts and offsets. Double-click a link to locate it in Resources.");
    explainHint->setWordWrap(true); explainLayout->addWidget(explainHint);
    connect(collapse, &QPushButton::clicked, explanation_, &QTreeWidget::collapseAll);
    browserTabs_->addTab(explainPage, "Explain"); splitter->addWidget(browserTabs_);
    auto* inspector = new QFrame; inspector->setObjectName("card"); auto* inspectorLayout = new QVBoxLayout(inspector); inspectorLayout->setContentsMargins(14, 14, 14, 14); inspectorLayout->setSpacing(10);
    auto* inspectorHeader = new QHBoxLayout; previewTitle_ = new QLabel("Resource viewer"); previewTitle_->setObjectName("section"); inspectorHeader->addWidget(previewTitle_); inspectorHeader->addStretch();
    inspectorHeader->addWidget(actionButton(folder_, "Open folder"));
    references_ = new QPushButton("References…"); references_->setObjectName("resourceReferences"); references_->setEnabled(false);
    references_->setToolTip("Find references to and from this resource, including source scripts and byte offsets.");
    inspectorHeader->addWidget(references_);
    connect(references_, &QPushButton::clicked, this, &MainWindow::showResourceReferences);
    export_ = new QPushButton("Export…"); export_->setObjectName("exportResource"); export_->setEnabled(false); inspectorHeader->addWidget(export_);
    connect(export_, &QPushButton::clicked, this, &MainWindow::exportResource); inspectorLayout->addLayout(inspectorHeader);
    auto* controls = new QHBoxLayout;
    cels_ = new QComboBox; cels_->setObjectName("cels"); cels_->hide(); controls->addWidget(cels_, 1);
    play_ = new QPushButton("Play loop"); play_->setObjectName("playLoop"); play_->setCheckable(true); play_->setEnabled(false); play_->hide(); play_->setToolTip("Preview the selected loop at about 8 frames per second"); controls->addWidget(play_);
    playTimer_ = new QTimer(this); playTimer_->setInterval(120);
    connect(playTimer_, &QTimer::timeout, this, &MainWindow::advanceCel);
    connect(play_, &QPushButton::toggled, this, [this](bool active) { play_->setText(active ? "Pause" : "Play loop"); if (active) playTimer_->start(); else playTimer_->stop(); });
    editText_ = new QPushButton("Edit text…"); editText_->setObjectName("editText"); editText_->hide();
    controls->addWidget(editText_); connect(editText_, &QPushButton::clicked, this, &MainWindow::editText);
    inspectorLayout->addLayout(controls);
    auto* previewSplit = new QSplitter(Qt::Vertical); previewSplit->setObjectName("previewSplitter"); previewSplit->setHandleWidth(7); previewSplit->setChildrenCollapsible(false);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setMinimumHeight(80);
    image_ = new QLabel("Select a resource to preview"); image_->setAlignment(Qt::AlignCenter); image_->setMinimumHeight(60);
    image_->setStyleSheet("background: #202c38; color: #a9b8c7; padding: 12px;"); scroll->setWidget(image_);
    previewStack_ = new QStackedWidget; previewStack_->setObjectName("visualPreview"); previewStack_->addWidget(scroll); previewStack_->hide();
    strings_ = new QTableWidget(0, 2); strings_->setObjectName("textStrings");
    strings_->setHorizontalHeaderLabels({"String", "Text"}); strings_->verticalHeader()->hide();
    strings_->horizontalHeader()->setStretchLastSection(true); strings_->setColumnWidth(0, 60);
    strings_->setEditTriggers(QAbstractItemView::NoEditTriggers); strings_->setSelectionBehavior(QAbstractItemView::SelectRows);
    strings_->setWordWrap(true); strings_->setAlternatingRowColors(true);
    previewStack_->addWidget(strings_); previewSplit->addWidget(previewStack_);
    details_ = new QPlainTextEdit; details_->setObjectName("details"); details_->setReadOnly(true); details_->setMinimumHeight(80);
    details_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); details_->setPlaceholderText("Resource details, script disassembly and binary previews appear here.");
    previewSplit->addWidget(details_); previewSplit->setStretchFactor(0, 3); previewSplit->setStretchFactor(1, 1); previewSplit->setSizes({420, 170});
    inspectorLayout->addWidget(previewSplit, 1); splitter->addWidget(inspector); splitter->setStretchFactor(0, 2); splitter->setStretchFactor(1, 3); splitter->setSizes({420, 650}); layout->addWidget(splitter, 1);
    auto* note = new QLabel("Edit exported PNGs in your image editor, refresh to review, then build to a new folder."); note->setObjectName("location"); note->setWordWrap(true); layout->addWidget(note);
    setCentralWidget(central);
    connect(browserTabs_, &QTabWidget::currentChanged, this, [this](int tab) {
        if (tab == 1) previewExplanation();
        else preview();
    });
    connect(explanation_, &QTreeWidget::currentItemChanged, this, [this] {
        if (browserTabs_->currentIndex() == 1) previewExplanation();
    });
    connect(explanation_, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (explanation_->selection().resource && export_->isEnabled()) browserTabs_->setCurrentIndex(0);
    });
    connect(filter_, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { preview(); });
    connect(cels_, &QComboBox::currentIndexChanged, this, [this] { previewCel(); });
    for (auto* action : {refresh_, folder_, build_, close_, rename_, explain_, gameInfo_}) action->setEnabled(false);
    updateRecents();
    restoreGeometry(QSettings().value("windowGeometry").toByteArray());
}
