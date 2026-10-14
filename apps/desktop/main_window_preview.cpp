// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.hpp"
#include "font_preview.hpp"
#include "resource_preview.hpp"
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
void MainWindow::preview() {
    play_->setChecked(false); play_->setEnabled(false); export_->setEnabled(false); references_->setEnabled(false);
    play_->hide(); previewStack_->hide(); previewTitle_->setText("Resource viewer");
    editText_->hide(); strings_->setRowCount(0); previewStack_->setCurrentIndex(0);
    clearResourcePreview(previewStack_);
    previewImage_ = {}; previewView_ = {}; previewPicture_ = {};
    selectedView_.clear(); selectedPicture_.clear(); cels_->clear(); cels_->hide(); image_->clear(); details_->clear();
    const auto selected = proxy_->mapToSource(table_->currentIndex());
    if (!project_ || !selected.isValid() || static_cast<std::size_t>(selected.row()) >= project_->game.resources.size()) {
        details_->setPlainText("Select a resource to inspect."); return;
    }
    const auto index = static_cast<std::size_t>(selected.row());
    const auto& resource = project_->game.resources.at(index);
    export_->setEnabled(true);
    references_->setEnabled(!busy_);
    QString description = "Record " + QString::number(index) + " • " + QString::fromUtf8(scimate::resource_type_name(resource.entry.id.type).data()) + " " + QString::number(resource.entry.id.number) +
        "\nPayload: payloads/" + stem(index) + ".bin\n";
    std::error_code directoryError;
    if (std::filesystem::is_directory(project_->directory / "assets" / stem(index).toStdString(), directoryError))
        description += "Editable/binary bundle: assets/" + stem(index) + "\nOpen Resource Folder to edit, then Refresh from Disk.\n";
    if (directoryError && directoryError != std::errc::no_such_file_or_directory)
        description += "Bundle unavailable: " + QString::fromStdString(directoryError.message()) + "\n";
    try {
        if (resource.entry.id.type == scimate::ResourceType::view) {
            previewView_ = scimate::read_sci0_view(resource.payload); const auto& view = previewView_; selectedView_ = resource.payload;
            description += "Editable folder: views/" + stem(index) + "\nPreview reflects the last successful refresh.\n";
            for (std::size_t loop = 0; loop < view.loops.size(); ++loop) {
                const auto& entry = view.loops[loop]; const auto& source = entry.mirror_of < 0 ? entry : view.loops.at(static_cast<std::size_t>(entry.mirror_of));
                for (std::size_t cel = 0; cel < source.cels.size(); ++cel)
                    cels_->addItem(QString("Loop %1 / Cel %2%3").arg(loop).arg(cel).arg(entry.mirror_of < 0 ? "" : " (mirrored)"), QPoint(static_cast<int>(loop), static_cast<int>(cel)));
            }
            previewTitle_->setText("View animation");
            cels_->setVisible(cels_->count() > 0); play_->setVisible(cels_->count() > 0); play_->setEnabled(cels_->count() > 0); previewCel();
            previewStack_->setVisible(!previewImage_.isNull());
            play_->setChecked(!busy_ && !previewImage_.isNull() && preferences::animateViews());
        } else if (resource.entry.id.type == scimate::ResourceType::pic) {
            previewPicture_ = scimate::read_sci0_picture(resource.payload);
            selectedPicture_ = resource.payload;
            description += "Editable folder: pictures/" + stem(index) + "\nMask colors represent EGA indices 0–15.\nPreview reflects the last successful refresh.\n";
            cels_->addItems({"Visual background", "Priority mask", "Control mask"});
            previewTitle_->setText("Background and masks");
            cels_->show(); previewCel(); previewStack_->show();
        } else if (resource.entry.id.type == scimate::ResourceType::text) {
            const auto value = QString::fromStdString(scimate::export_sci0_text_table(resource.payload));
            const auto rows = value.split('\n');
            strings_->setRowCount(rows.size() - 2);
            for (int row = 1; row < rows.size() - 1; ++row) {
                const auto fields = rows[row].split('\t');
                strings_->setItem(row - 1, 0, new QTableWidgetItem(fields[0]));
                strings_->setItem(row - 1, 1, new QTableWidgetItem(fields[1]));
            }
            previewTitle_->setText("Text strings");
            strings_->resizeRowsToContents(); previewStack_->setCurrentWidget(strings_); previewStack_->show(); editText_->show();
            description += QString("\n%1 numbered strings. Edit text… saves directly to the project.\n").arg(strings_->rowCount());
            description += "Text is shown losslessly: \\n, \\r, \\t, \\\\ and \\xHH byte escapes.\nString indices are script references; keep their order. Export as TSV for external editing.";
        } else if (resource.entry.id.type == scimate::ResourceType::font) {
            previewTitle_->setText("Font glyphs");
            description += showFontPreview(previewStack_, resource.payload);
        } else if (resource.entry.id.type == scimate::ResourceType::script) {
            previewTitle_->setText("Script disassembly");
            description += "\n" + QString::fromStdString(scimate::disassemble_sci0_script(resource.payload, resource.entry.id.number));
        } else {
            previewTitle_->setText("Binary resource");
            description += "\nHexadecimal preview (first 512 bytes):\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(resource.payload.size(), 512); ++i) {
                if (i % 16 == 0) description += QString("\n%1  ").arg(i, 4, 16, QChar('0'));
                description += QString("%1 ").arg(resource.payload[i], 2, 16, QChar('0'));
            }
        }
    } catch (...) {
        const auto error = scimate::exception_message(std::current_exception());
        scimate::log_diagnostic(scimate::LogLevel::warning, "Resource preview", error);
        description += "\nPreview unavailable: " + QString::fromStdString(error);
    }
    details_->setPlainText(description);
}

void MainWindow::previewCel() {
    const auto& palette = scimate::format::ega_rgb;
    if (!selectedPicture_.empty() && cels_->currentIndex() >= 0) {
        const auto& picture = previewPicture_;
        const auto& pixels = picture.planes.at(static_cast<std::size_t>(cels_->currentIndex()));
        QImage image(scimate::format::picture_width, scimate::format::picture_height, QImage::Format_RGB32);
        for (int y = 0; y < scimate::format::picture_height; ++y) for (int x = 0; x < scimate::format::picture_width; ++x)
            image.setPixel(x, y, 0xff000000u | palette[pixels[static_cast<std::size_t>(y) * scimate::format::picture_width + x]]);
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
    } else if (format == "tsv" && resource.entry.id.type == scimate::ResourceType::text)
        data = QByteArray::fromStdString(scimate::export_sci0_text_table(resource.payload));
    else if (format == "asm" && resource.entry.id.type == scimate::ResourceType::script)
        data = QByteArray::fromStdString(scimate::disassemble_sci0_script(resource.payload, resource.entry.id.number));
    else throw std::runtime_error("This export format is not available for the selected resource.");
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit())
        throw std::runtime_error(("Could not export resource: " + output.errorString()).toStdString());
}
