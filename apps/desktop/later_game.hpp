// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include "scimate/picture.hpp"
#include "scimate/view.hpp"
#include <QDialog>
#include <QImage>
#include <optional>
class QLineEdit;
class QComboBox;
class QPushButton;
class QTreeWidget;
class QLabel;
class QPlainTextEdit;
class LaterGameDialog : public QDialog {
public:
    explicit LaterGameDialog(QWidget* parent = nullptr);
private:
    void load();
    void preview();
    void frame();
    void exportResource();
    std::optional<scimate::Game> game_;
    int loadedFormat_ = 0;
    QLineEdit *source_, *filter_;
    QComboBox *format_, *frames_;
    QPushButton *load_, *browse_, *export_;
    QTreeWidget* resources_;
    QLabel *status_, *image_;
    QPlainTextEdit* details_;
    scimate::Picture picture_;
    scimate::View view_;
    scimate::PicturePalette palette_ = scimate::ega_picture_palette();
    bool showingPicture_ = false;
    QImage rendered_;
};
