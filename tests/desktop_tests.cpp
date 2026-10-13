// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.hpp"
#include "project_fixture.hpp"
#include <QtTest>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QImage>
#include <QMessageBox>
#include <QTableView>
#include <QTemporaryDir>

class DesktopTests : public QObject {
    Q_OBJECT
private slots:
    void projectWorkflow() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QCoreApplication::setOrganizationName("SCIMateTests");
        QCoreApplication::setApplicationName("DesktopTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        const auto source = temp.path() + "/source";
        project_fixture::create(std::filesystem::u8path(source.toUtf8().constData()));
        MainWindow window; window.show();
        window.createProject(source, temp.path() + "/workspace", "My project");
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 10000);
        QVERIFY(window.project());
        QCOMPARE(window.project()->name, std::string("My project"));
        auto* table = window.findChild<QTableView*>("resources");
        auto* filter = window.findChild<QLineEdit*>("resourceFilter");
        auto* details = window.findChild<QPlainTextEdit*>("details");
        QVERIFY(table && filter && details);
        QCOMPARE(table->model()->rowCount(), 6);
        filter->setText("view"); QCOMPARE(table->model()->rowCount(), 1);
        table->selectRow(0);
        QVERIFY(window.findChild<QComboBox*>("cels")->count() > 0);
        QVERIFY(details->toPlainText().contains("Editable folder"));
        filter->setText("pic"); QCOMPARE(table->model()->rowCount(), 1); table->selectRow(0);
        auto* planes = window.findChild<QComboBox*>("cels"); QCOMPARE(planes->count(), 3);
        planes->setCurrentIndex(2); QCOMPARE(planes->currentText(), QString("Control mask"));
        QVERIFY(details->toPlainText().contains("Mask colors"));
        filter->setText("text"); QCOMPARE(table->model()->rowCount(), 2);
        table->selectRow(0); QVERIFY(details->toPlainText().contains("Hexadecimal"));
        project_fixture::write(window.project()->directory / "payloads/0000000.bin", {'n','e','w'});
        const auto pngPath = temp.path() + "/workspace/views/0000003/loop-0-cel-0.png";
        QImage png(pngPath); QVERIFY(!png.isNull());
        png = png.convertToFormat(QImage::Format_ARGB32); png.setPixelColor(0, 0, Qt::white);
        QVERIFY(png.save(pngPath));
        window.openProject(temp.path() + "/workspace");
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 10000);
        filter->setText("Modified"); QCOMPARE(table->model()->rowCount(), 2);
        window.buildProject(temp.path() + "/built");
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 10000);
        const auto built = scimate::open_sci0_game(std::filesystem::u8path((temp.path() + "/built").toUtf8().constData()));
        QVERIFY(built.resources[0].payload == scimate::Bytes({'n','e','w'}));
        QCOMPARE(scimate::read_sci0_view(built.resources[3].payload).loops[0].cels[0].pixels[0], 15);
        bool sawError = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                sawError = true; box->accept();
            }
        });
        dismiss.start(10);
        window.openProject(temp.path() + "/missing");
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 10000);
        QVERIFY(sawError); QCOMPARE(window.project()->name, std::string("My project"));
        dismiss.stop();
        const auto realProject = qEnvironmentVariable("SCIMATE_TEST_PROJECT");
        if (!realProject.isEmpty()) {
            window.openProject(realProject);
            QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 60000);
            QVERIFY(window.project());
            filter->setText("view"); QVERIFY(table->model()->rowCount() > 0); table->selectRow(1); table->scrollTo(table->model()->index(1, 0));
        }
        const auto screenshot = qEnvironmentVariable("SCIMATE_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(100); QVERIFY(window.grab().save(screenshot)); }
    }
};
QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
