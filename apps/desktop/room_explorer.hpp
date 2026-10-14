// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/project.hpp"
#include "scimate/rooms.hpp"
#include <QWidget>
#include <functional>
#include <memory>
class QTreeWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QGraphicsScene;
class QGraphicsView;
class RoomExplorerWidget : public QWidget {
public:
    RoomExplorerWidget(std::shared_ptr<const scimate::Project> project,
        std::function<void(scimate::ResourceId)> openResource, QWidget* parent = nullptr);
private:
    void install(scimate::RoomExplorer analysis);
    void selectRoom();
    void drawRoom();
    void drawMap();
    void followExit();
    std::shared_ptr<const scimate::Project> project_;
    std::function<void(scimate::ResourceId)> openResource_;
    scimate::RoomExplorer analysis_;
    std::optional<std::size_t> selected_;
    QTreeWidget *rooms_, *links_, *objects_;
    QLineEdit* search_;
    QComboBox* plane_;
    QCheckBox* sprites_;
    QLabel* status_;
    QPlainTextEdit* evidence_;
    QGraphicsScene *scene_, *mapScene_;
    QGraphicsView *canvas_, *map_;
};
