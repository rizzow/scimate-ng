// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/project.hpp"
#include <QMainWindow>
#include <functional>
#include <memory>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QProgressBar;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTableView;
class QCloseEvent;

class MainWindow : public QMainWindow {
public:
    MainWindow();
    void openProject(const QString& directory);
    void createProject(const QString& source, const QString& directory, const QString& name);
    void buildProject(const QString& destination);
    bool busy() const { return busy_; }
    const scimate::Project* project() const { return project_.get(); }
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    using ProjectPtr = std::shared_ptr<scimate::Project>;
    void run(const QString& message, std::function<ProjectPtr()> operation);
    void installProject(ProjectPtr project);
    void newProjectDialog();
    void updateRecents();
    void preview();
    void previewCel();
    void showFolder();
    ProjectPtr project_;
    bool busy_ = false;
    QAction *new_, *open_, *refresh_, *build_, *folder_, *close_;
    QMenu* recent_;
    QLabel *heading_, *location_, *image_;
    QLineEdit* filter_;
    QComboBox* cels_;
    QPlainTextEdit* details_;
    QTableView* table_;
    QStandardItemModel* model_;
    QSortFilterProxyModel* proxy_;
    QProgressBar* progress_;
    scimate::Bytes selectedView_, selectedPicture_;
};
