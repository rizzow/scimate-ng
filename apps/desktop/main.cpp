// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.hpp"
#include <QApplication>
#include <QFileInfo>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SCIMate");
    QCoreApplication::setApplicationName("SCIMate NG");
    MainWindow window;
    window.show();
    if (argc == 2) {
        const QFileInfo input(QString::fromLocal8Bit(argv[1]));
        const auto directory = input.isDir() ? input.absoluteFilePath() : input.absolutePath();
        QTimer::singleShot(0, &window, [&window, directory] { window.openProject(directory); });
    }
    return app.exec();
}
