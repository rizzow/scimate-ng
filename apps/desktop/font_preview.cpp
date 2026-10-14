// SPDX-License-Identifier: GPL-2.0-or-later
#include "font_preview.hpp"
#include "resource_preview.hpp"
#include "scimate/assets.hpp"
#include <QtWidgets>
#include <memory>

namespace {
QImage glyphImage(const scimate::ViewCel& glyph) {
    QImage image(std::max(1u, unsigned(glyph.width)), std::max(1u, unsigned(glyph.height)), QImage::Format_RGB32);
    image.fill(Qt::white);
    for (unsigned y = 0; y < glyph.height; ++y)
        for (unsigned x = 0; x < glyph.width; ++x)
            image.setPixelColor(x, y, glyph.pixels[y * glyph.width + x] == 0 ? Qt::black : Qt::white);
    return image;
}
}

QString showFontPreview(QStackedWidget* stack, const scimate::Bytes& payload) {
    const auto font = std::make_shared<const scimate::Font>(scimate::read_sci0_font(payload));
    auto page = std::make_unique<QWidget>();
    auto* layout = new QVBoxLayout(page.get());
    auto* sample = new QLineEdit("SCIMate 0123456789");
    sample->setObjectName("fontSample"); sample->setMaxLength(80);
    sample->setPlaceholderText("Sample text (byte values 0–255)");
    layout->addWidget(sample);
    auto* sampleImage = new QLabel;
    sampleImage->setObjectName("fontSampleImage");
    auto* sampleScroll = new QScrollArea;
    sampleScroll->setWidget(sampleImage); sampleScroll->setWidgetResizable(false);
    sampleScroll->setMinimumHeight(70); layout->addWidget(sampleScroll);
    auto* glyphs = new QListWidget;
    glyphs->setObjectName("fontGlyphs"); glyphs->setViewMode(QListView::IconMode);
    glyphs->setResizeMode(QListView::Adjust); glyphs->setMovement(QListView::Static);
    glyphs->setIconSize(QSize(48, 48)); glyphs->setGridSize(QSize(90, 80));
    for (std::size_t i = 0; i < font->glyphs.size(); ++i) {
        const auto& glyph = font->glyphs[i];
        const auto image = glyphImage(glyph);
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(image.scaled(48, 48, Qt::KeepAspectRatio, Qt::FastTransformation))),
            QString("%1 · %2×%3").arg(i).arg(glyph.width).arg(glyph.height), glyphs);
        item->setToolTip(QString("Glyph %1 (0x%2), %3 × %4 pixels").arg(i).arg(i, 2, 16, QChar('0')).arg(glyph.width).arg(glyph.height));
    }
    layout->addWidget(glyphs, 1);
    auto* selected = new QLabel("Select a glyph");
    selected->setObjectName("fontSelectedGlyph"); selected->setAlignment(Qt::AlignCenter);
    layout->addWidget(selected);
    QObject::connect(glyphs, &QListWidget::currentRowChanged, page.get(), [font, selected](int row) {
        if (row < 0 || static_cast<std::size_t>(row) >= font->glyphs.size()) return;
        const auto image = glyphImage(font->glyphs[row]);
        selected->setPixmap(QPixmap::fromImage(image.scaled((image.size() * 4).boundedTo(QSize(256, 128)), Qt::KeepAspectRatio, Qt::FastTransformation)));
    });
    const auto updateSample = [font, sample, sampleImage] {
        int width = 8, height = font->line_height + 8;
        for (const auto c : sample->text()) {
            if (c.unicode() < font->glyphs.size()) {
                const auto& glyph = font->glyphs[c.unicode()];
                width += glyph.width; height = std::max(height, int(glyph.height) + 8);
            } else width += 8;
        }
        QImage image(width, height, QImage::Format_RGB32); image.fill(Qt::white);
        QPainter painter(&image); painter.setPen(Qt::red);
        int x = 4;
        for (const auto c : sample->text()) {
            if (c.unicode() < font->glyphs.size()) {
                const auto& glyph = font->glyphs[c.unicode()];
                if (glyph.width && glyph.height) painter.drawImage(x, 4, glyphImage(glyph));
                x += glyph.width;
            } else { painter.drawRect(x, 4, 6, 8); x += 8; }
        }
        painter.end();
        sampleImage->setPixmap(QPixmap::fromImage(image.scaled(image.size() * 2, Qt::KeepAspectRatio, Qt::FastTransformation)));
        sampleImage->adjustSize();
    };
    QObject::connect(sample, &QLineEdit::textChanged, page.get(), updateSample);
    updateSample();
    if (!font->glyphs.empty()) glyphs->setCurrentRow(0);
    installResourcePreview(stack, page.release());
    return QString("\n%1 glyphs · line height %2 pixels.\nSample uses character byte values; missing glyphs appear as red boxes. The game's code page is not inferred.").arg(font->glyphs.size()).arg(font->line_height);
}
