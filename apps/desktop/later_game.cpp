// SPDX-License-Identifier: GPL-2.0-or-later
#include "later_game.hpp"
#include "scimate/sci1_io.hpp"
#include "scimate/diagnostics.hpp"
#include "diagnostics.hpp"
#include "paths.hpp"
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
namespace {
struct LoadedGame { std::optional<scimate::Game> game; QString error; };
}
LaterGameDialog::LaterGameDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Inspect SCI1 / SCI1.1 Game — read only"); resize(1100, 760);
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    source_ = new QLineEdit; source_->setPlaceholderText("Game folder containing resource.map"); controls->addWidget(source_, 1);
    browse_ = new QPushButton("Browse…"); controls->addWidget(browse_);
    format_ = new QComboBox;
    format_->addItems({"SCI1 packed VGA", "SCI1 packed EGA", "SCI1 late VGA", "SCI1.1"}); controls->addWidget(format_);
    load_ = new QPushButton("Inspect"); controls->addWidget(load_); layout->addLayout(controls);
    status_ = new QLabel("Choose the resource format explicitly. Graphics previews support the implemented SCI1 VGA subset; other layouts retain binary inspection.");
    status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    auto* split = new QSplitter; layout->addWidget(split, 1);
    auto* left = new QWidget; auto* list = new QVBoxLayout(left);
    filter_ = new QLineEdit; filter_->setPlaceholderText("Filter resource type or number…"); list->addWidget(filter_);
    resources_ = new QTreeWidget; resources_->setHeaderLabels({"Type", "Number", "Bytes"}); resources_->setRootIsDecorated(false); list->addWidget(resources_, 1); split->addWidget(left);
    auto* right = new QWidget; auto* previewLayout = new QVBoxLayout(right);
    auto* toolbar = new QHBoxLayout; frames_ = new QComboBox; toolbar->addWidget(frames_, 1);
    export_ = new QPushButton("Export selected…"); export_->setEnabled(false); toolbar->addWidget(export_); previewLayout->addLayout(toolbar);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); image_ = new QLabel; image_->setAlignment(Qt::AlignCenter); scroll->setWidget(image_); previewLayout->addWidget(scroll, 2);
    details_ = new QPlainTextEdit; details_->setReadOnly(true); details_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); previewLayout->addWidget(details_, 1); split->addWidget(right); split->setSizes({330, 700});
    connect(browse_, &QPushButton::clicked, this, [this] {
        const auto selected = QFileDialog::getExistingDirectory(this, "SCI1 game folder", desktop_detail::dialogDirectory("games"));
        if (!selected.isEmpty()) { source_->setText(selected); source_->setCursorPosition(0); desktop_detail::rememberDirectory("games", selected); }
    });
    connect(load_, &QPushButton::clicked, this, [this] { load(); });
    connect(resources_, &QTreeWidget::currentItemChanged, this, [this] { preview(); });
    connect(frames_, &QComboBox::currentIndexChanged, this, [this] { frame(); });
    connect(export_, &QPushButton::clicked, this, [this] { exportResource(); });
    connect(filter_, &QLineEdit::textChanged, this, [this](const QString& value) {
        for (int i = 0; i < resources_->topLevelItemCount(); ++i) {
            auto* item = resources_->topLevelItem(i); item->setHidden(!(item->text(0) + " " + item->text(1)).contains(value, Qt::CaseInsensitive));
        }
    });
}
void LaterGameDialog::load() {
    const auto source = desktop_detail::path(source_->text().trimmed());
    if (source.empty()) return;
    const int selected = format_->currentIndex();
    const auto format = selected < 2 ? scimate::Sci1Format::sci1_ega : selected == 2 ? scimate::Sci1Format::sci1_late : scimate::Sci1Format::sci11;
    for (auto* widget : {static_cast<QWidget*>(source_), static_cast<QWidget*>(format_), static_cast<QWidget*>(browse_), static_cast<QWidget*>(load_)}) widget->setEnabled(false);
    status_->setText("Reading map, volumes and decoding resources…");
    auto* watcher = new QFutureWatcher<LoadedGame>(this);
    connect(watcher, &QFutureWatcher<LoadedGame>::finished, this, [this, watcher, selected] {
        watcher->deleteLater();
        source_->setEnabled(true); format_->setEnabled(true); browse_->setEnabled(true); load_->setEnabled(true);
        auto result = watcher->result();
        if (!result.game) { status_->setText("Inspection failed: " + result.error); return; }
        game_ = std::move(result.game); loadedFormat_ = selected;
        const QSignalBlocker block(resources_); resources_->clear(); filter_->clear();
        for (std::size_t i = 0; i < game_->resources.size(); ++i) {
            const auto& resource = game_->resources[i];
            auto* item = new QTreeWidgetItem(resources_, {QString::fromStdString(std::string(scimate::resource_type_name(resource.entry.id.type))), QString::number(resource.entry.id.number), QString::number(resource.payload.size())});
            item->setData(0, Qt::UserRole, static_cast<int>(i));
        }
        status_->setText(QString("%1 decoded records · %2 · read only. Container validation does not establish gameplay compatibility.").arg(game_->resources.size()).arg(format_->itemText(selected)));
        if (resources_->topLevelItemCount()) resources_->setCurrentItem(resources_->topLevelItem(0));
        preview();
    });
    watcher->setFuture(QtConcurrent::run([source, format] {
        try { return LoadedGame{scimate::open_sci1_game(source, format), {}}; }
        catch (...) { return LoadedGame{{}, QString::fromStdString(scimate::exception_message(std::current_exception()))}; }
    }));
}
void LaterGameDialog::preview() {
    image_->clear(); rendered_ = {}; view_ = {}; picture_ = {}; showingPicture_ = false; frames_->clear(); export_->setEnabled(false); details_->clear();
    const auto* item = resources_->currentItem(); if (!game_ || !item) return;
    const auto& resource = game_->resources.at(item->data(0, Qt::UserRole).toUInt()); export_->setEnabled(true);
    QString detail = "Decoded payload — read only\n";
    try {
        if (resource.entry.id.type == scimate::ResourceType::pic && loadedFormat_ != 3) {
            if (loadedFormat_ == 1) { picture_ = scimate::read_sci0_picture(resource.payload); palette_ = scimate::ega_picture_palette(); }
            else { const auto decoded = scimate::read_sci1_vga_picture(resource.payload); picture_ = decoded.picture; palette_ = decoded.palette; }
            showingPicture_ = true; frames_->addItems({"Visual", "Priority", "Control"}); frame();
        } else if (resource.entry.id.type == scimate::ResourceType::view && loadedFormat_ != 3) {
            view_ = loadedFormat_ == 1 ? scimate::read_sci0_view(resource.payload) : scimate::read_sci1_vga_view(resource.payload);
            if (loadedFormat_ != 1 && !view_.palette) throw std::runtime_error("View requires a shared game palette; this inspector only renders embedded palettes.");
            palette_ = view_.palette ? *view_.palette : scimate::ega_picture_palette();
            for (std::size_t l = 0; l < view_.loops.size(); ++l) {
                const auto& loop = view_.loops[l]; const auto& source = loop.mirror_of < 0 ? loop : view_.loops.at(loop.mirror_of);
                for (std::size_t c = 0; c < source.cels.size(); ++c) frames_->addItem(QString("Loop %1 / Cel %2").arg(l).arg(c), QPoint(static_cast<int>(l), static_cast<int>(c)));
            }
            frame();
        }
    } catch (...) { detail += "Graphics preview unavailable: " + QString::fromStdString(scimate::exception_message(std::current_exception())) + "\n"; }
    detail += "Hexadecimal preview (first 512 bytes):\n";
    for (std::size_t i = 0; i < std::min<std::size_t>(512, resource.payload.size()); ++i) {
        if (i % 16 == 0) detail += QString("\n%1  ").arg(i, 4, 16, QChar('0'));
        detail += QString("%1 ").arg(resource.payload[i], 2, 16, QChar('0'));
    }
    details_->setPlainText(detail);
}
void LaterGameDialog::frame() {
    if (frames_->currentIndex() < 0) return;
    scimate::ViewCel cel; bool mirror = false;
    if (showingPicture_) { cel.width = 320; cel.height = 190; cel.pixels = picture_.planes.at(frames_->currentIndex()); }
    else {
        const auto position = frames_->currentData().toPoint(); if (view_.loops.empty()) return;
        const auto& loop = view_.loops.at(position.x()); mirror = loop.mirror_of >= 0;
        cel = view_.loops.at(mirror ? loop.mirror_of : position.x()).cels.at(position.y());
    }
    QImage image(cel.width, cel.height, QImage::Format_ARGB32);
    for (int y = 0; y < cel.height; ++y) for (int x = 0; x < cel.width; ++x) {
        const auto index = cel.pixels.at(static_cast<std::size_t>(y * cel.width + x)); const auto& rgb = palette_[index];
        image.setPixel(mirror ? cel.width - x - 1 : x, y, !showingPicture_ && index == cel.transparent ? 0 : qRgb(rgb[0], rgb[1], rgb[2]));
    }
    rendered_ = image; image_->setPixmap(QPixmap::fromImage(image.scaled(image.width()*2, image.height()*2, Qt::KeepAspectRatio, Qt::FastTransformation)));
}
void LaterGameDialog::exportResource() {
    const auto* item = resources_->currentItem(); if (!game_ || !item) return;
    const auto& resource = game_->resources.at(item->data(0, Qt::UserRole).toUInt());
    QString filter;
    const auto destination = QFileDialog::getSaveFileName(this, "Export inspected resource", {}, rendered_.isNull() ? "Decoded payload (*.bin)" : "Decoded payload (*.bin);;Selected image (*.png)", &filter);
    if (destination.isEmpty()) return;
    QByteArray bytes;
    if (filter.contains("*.png")) { QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); if (!rendered_.save(&buffer, "PNG")) throw std::runtime_error("PNG encoding failed"); }
    else bytes = QByteArray(reinterpret_cast<const char*>(resource.payload.data()), static_cast<int>(resource.payload.size()));
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        desktop_detail::show_error(this, "Export failed", output.errorString());
}
