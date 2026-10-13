// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.hpp"
#include "diagnostics.hpp"
#include <QIcon>
#include <QApplication>
#include <QFileInfo>
#include <QTimer>

namespace {
class Application final : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject* receiver, QEvent* event) override {
        try { return QApplication::notify(receiver, event); }
        catch (...) {
            const auto message = scimate::exception_message(std::current_exception());
            if (reporting_) {
                scimate::log_diagnostic(scimate::LogLevel::error, "Desktop event", message);
                return false;
            }
            reporting_ = true;
            try { desktop_detail::show_error(activeWindow(), "Desktop event failed", QString::fromStdString(message)); }
            catch (...) { scimate::log_diagnostic(scimate::LogLevel::error, "Desktop error reporting", message); }
            reporting_ = false;
            return false;
        }
    }
private:
    bool reporting_ = false;
};
}

int main(int argc, char** argv) {
    Application app(argc, argv);
    QApplication::setWindowIcon(QIcon(":/scimate.png"));
    QCoreApplication::setOrganizationName("SCIMate");
    QCoreApplication::setApplicationName("SCIMate NG");
    try {
        MainWindow window;
        window.show();
        if (argc == 2) {
            const QFileInfo input(QString::fromLocal8Bit(argv[1]));
            const auto directory = input.isDir() ? input.absoluteFilePath() : input.absolutePath();
            QTimer::singleShot(0, &window, [&window, directory] { window.openProject(directory); });
        }
        return app.exec();
    } catch (...) {
        desktop_detail::show_exception(nullptr, "SCIMate startup failed");
        return 1;
    }
}
