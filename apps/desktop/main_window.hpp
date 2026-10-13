// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/project.hpp"
#include "scimate/view.hpp"
#include "scimate/picture.hpp"
#include <QMainWindow>
#include <QElapsedTimer>
#include <QImage>
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
class QPushButton;
class QTimer;
class QTableWidget;
class QStackedWidget;

class MainWindow : public QMainWindow {
public:
    MainWindow();
    void openProject(const QString& directory);
    void createProject(const QString& source, const QString& directory, const QString& name);
    void buildProject(const QString& destination);
    void exportSelected(const QString& destination, const QString& format);
    void renameProject(const QString& name);
    void saveSelectedText(const QString& table);
    bool busy() const { return busy_; }
    const scimate::Project* project() const { return project_.get(); }
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    using ProjectPtr = std::shared_ptr<scimate::Project>;
    void run(const QString& message, std::function<ProjectPtr(const scimate::ProgressCallback&)> operation);
    void installProject(ProjectPtr project, const std::vector<bool>& modified);
    void newProjectDialog();
    void updateRecents();
    void preview();
    void previewCel();
    void showFolder();
    void exportResource();
    void advanceCel();
    void editText();
    ProjectPtr project_;
    bool busy_ = false;
    QAction *new_, *open_, *refresh_, *build_, *folder_, *close_, *rename_;
    QMenu* recent_;
    QLabel *heading_, *location_, *image_, *progressTitle_, *progressDetail_;
    QWidget *progressPanel_;
    QTimer *progressTimer_, *playTimer_;
    QElapsedTimer elapsed_;
    QPushButton *play_, *export_, *editText_;
    QTableWidget* strings_;
    QStackedWidget* previewStack_;
    QLineEdit* filter_;
    QComboBox *cels_, *types_;
    QPlainTextEdit* details_;
    QTableView* table_;
    QStandardItemModel* model_;
    QSortFilterProxyModel* proxy_;
    QProgressBar* progress_;
    QImage previewImage_;
    scimate::View previewView_;
    scimate::Picture previewPicture_;
    scimate::Bytes selectedView_, selectedPicture_;
};
