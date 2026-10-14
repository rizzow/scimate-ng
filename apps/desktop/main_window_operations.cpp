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
struct Outcome { std::shared_ptr<scimate::Project> project; QString error; std::vector<bool> modified; scimate::GameExplanation explanation; };
struct ProgressState { std::mutex mutex; scimate::OperationProgress value; };
}
void MainWindow::finishOperation() {
    busy_ = false; progressTimer_->stop(); progressPanel_->hide();
    new_->setEnabled(true); open_->setEnabled(true); recent_->setEnabled(true);
    browserTabs_->setEnabled(true);
    for (auto* action : {refresh_, folder_, build_, close_, rename_, explain_, gameInfo_}) action->setEnabled(bool(project_));
    cels_->setEnabled(true); editText_->setEnabled(true);
}

void MainWindow::run(const QString& message, std::function<ProjectPtr(const scimate::ProgressCallback&)> operation, bool explain) {
    if (busy_) return;
    busy_ = true;
    for (auto* action : {new_, open_, refresh_, folder_, build_, close_, rename_, explain_, gameInfo_}) action->setEnabled(false);
    browserTabs_->setEnabled(false);
    play_->setChecked(false); play_->setEnabled(false);
    cels_->setEnabled(false); export_->setEnabled(false); editText_->setEnabled(false); references_->setEnabled(false);
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
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, message, explain] {
        watcher->deleteLater();
        finishOperation();
        try {
            const auto result = watcher->result();
            if (result.error.isEmpty()) {
                if (explain) installExplanation(result.explanation);
                else if (result.project) installProject(result.project, result.modified);
                statusBar()->showMessage(message + QString(" — done in %1 s").arg(elapsed_.elapsed() / 1000.0, 0, 'f', 1), 10000);
                if (!migrationReport_.isEmpty()) {
                    QFile report(migrationReport_);
                    if (report.open(QIODevice::ReadOnly)) {
                        auto* dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose);
                        dialog->setWindowTitle("SCI1 migration report — runtime unverified"); dialog->resize(820, 640);
                        auto* layout = new QVBoxLayout(dialog); auto* contents = new QPlainTextEdit;
                        contents->setReadOnly(true); contents->setPlainText(QString::fromUtf8(report.read(16 * 1024 * 1024)));
                        layout->addWidget(contents); dialog->show();
                    }
                }
            } else {
                referenceFocus_.reset();
                statusBar()->showMessage("Operation failed; previous project remains open.");
                desktop_detail::show_error(this, message, result.error);
            }
        } catch (...) {
            referenceFocus_.reset();
            statusBar()->showMessage("Operation failed.");
            desktop_detail::show_exception(this, message);
        }
        finishOperation();
        migrationReport_.clear();
        if (browserTabs_->currentIndex() == 1) previewExplanation();
        else preview();
    });
    try {
        watcher->setFuture(QtConcurrent::run([operation = std::move(operation), progress, explain] {
            try {
                auto project = operation(progress);
                if (explain) {
                    progress({"Analyzing resource references", 0, 0, "Inspecting scripts, properties and resource links"});
                    return Outcome{project, {}, {}, scimate::analyze_sci0_game(project->game)};
                }
                std::vector<bool> modified;
                if (project) {
                    progress({"Preparing resource browser", 0, 0, "Comparing current resources with originals"});
                    modified = scimate::project_modified_records(*project);
                }
                return Outcome{std::move(project), {}, std::move(modified), {}};
            }
            catch (...) { return Outcome{{}, QString::fromStdString(scimate::exception_message(std::current_exception())), {}, {}}; }
        }));
    } catch (...) {
        watcher->deleteLater(); finishOperation();
        throw;
    }
}
