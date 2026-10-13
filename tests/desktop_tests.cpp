// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.hpp"
#include "explanation_tree.hpp"
#include "project_fixture.hpp"
#include <QtTest>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QImage>
#include <QPushButton>
#include <QSplitter>
#include <QProgressBar>
#include <QMessageBox>
#include <QTableView>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTabWidget>
#include <QAction>
#include "scimate/asset_io.hpp"
#include <QTemporaryDir>

class DesktopTests : public QObject {
    Q_OBJECT
private slots:
    void explanationReferences() {
        ExplanationTree tree;
        tree.setReport("Views\n  [fact] View 5: 2 loops\n    [fact] Loop 1: 2 cels\n"
                       "Scripts and objects\n  [fact] Script 3: 1 objects\n"
                       "    [fact] Initial pic = 6\n    [likely] Background picture reference 999 (resource absent)\n"
                       "Startup\n  [likely] Opening-room candidate 8: script 3 @ $0000 sends newRoom\n"
                       "  [unresolved] Room 8 has no proven resource identity\n");
        tree.setCurrentItem(tree.topLevelItem(0)->child(0)->child(0));
        QVERIFY(tree.selection().resource.has_value());
        QCOMPARE(tree.selection().resource->type, scimate::ResourceType::view);
        QCOMPARE(tree.selection().resource->number, 5);
        QCOMPARE(*tree.selection().loop, 1);
        tree.setCurrentItem(tree.topLevelItem(1)->child(0)->child(0));
        QCOMPARE(tree.selection().resource->type, scimate::ResourceType::pic);
        QCOMPARE(tree.selection().resource->number, 6);
        tree.setCurrentItem(tree.topLevelItem(2)->child(0));
        QCOMPARE(tree.selection().resource->type, scimate::ResourceType::script);
        QCOMPARE(tree.selection().resource->number, 3);
        tree.setCurrentItem(tree.topLevelItem(2)->child(1));
        QVERIFY(!tree.selection().resource);
    }
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
        QVERIFY(window.findChild<QWidget*>("progressPanel")->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 10000);
        QVERIFY(window.project());
        QCOMPARE(window.project()->name, std::string("My project"));
        window.renameProject("Renamed project");
        QCOMPARE(window.project()->name, std::string("Renamed project"));
        QVERIFY(window.windowTitle().contains("Renamed project"));
        QCOMPARE(scimate::open_project(window.project()->directory).name, std::string("Renamed project"));
        window.renameProject("My project");
        window.findChild<QAction*>("explainProject")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 10000);
        auto* explanation = window.findChild<QTreeWidget*>("explanationTree");
        QVERIFY(explanation && explanation->topLevelItemCount() > 0);
        QVERIFY(window.findChild<QWidget*>("visualPreview")->isHidden());
        const auto views = explanation->findItems("[fact] View 5:", Qt::MatchContains | Qt::MatchRecursive);
        QVERIFY(!views.empty()); explanation->setCurrentItem(views.front());
        QVERIFY(window.findChild<QWidget*>("visualPreview")->isVisible());
        QVERIFY(window.findChild<QPushButton*>("exportResource")->isEnabled());
        QVERIFY(window.findChild<QPlainTextEdit*>("details")->toPlainText().contains("[fact] View 5:"));
        window.findChild<QTabWidget*>()->setCurrentIndex(0);
        window.findChild<QTabWidget*>()->setCurrentIndex(1);
        QVERIFY(window.findChild<QWidget*>("visualPreview")->isVisible());
        window.findChild<QTabWidget*>()->setCurrentIndex(0);
        auto* table = window.findChild<QTableView*>("resources");
        auto* filter = window.findChild<QLineEdit*>("resourceFilter");
        auto* details = window.findChild<QPlainTextEdit*>("details");
        QVERIFY(table && filter && details);
        QCOMPARE(table->model()->rowCount(), 7);
        auto* types = window.findChild<QComboBox*>("resourceType");
        QVERIFY(types); types->setCurrentIndex(types->findData("view"));
        QCOMPARE(table->model()->rowCount(), 1);
        filter->setText("999999"); QCOMPARE(table->model()->rowCount(), 0);
        filter->clear(); QCOMPARE(table->model()->rowCount(), 1);
        table->selectRow(0);
        QVERIFY(window.findChild<QComboBox*>("cels")->count() > 0);
        QVERIFY(details->toPlainText().contains("Editable folder"));
        auto* play = window.findChild<QPushButton*>("playLoop");
        auto* frames = window.findChild<QComboBox*>("cels");
        const auto initial = frames->currentIndex(); play->click();
        QTRY_VERIFY_WITH_TIMEOUT(frames->currentIndex() != initial, 1000);
        QCOMPARE(frames->currentData().toPoint().x(), 0);
        play->click(); QVERIFY(!play->isChecked());
        window.exportSelected(temp.path() + "/cel.png", "png");
        QCOMPARE(QImage(temp.path() + "/cel.png").size(), QSize(3, 2));
        window.exportSelected(temp.path() + "/view.bin", "bin");
        QFile raw(temp.path() + "/view.bin"); QVERIFY(raw.open(QIODevice::ReadOnly));
        const auto viewBytes = raw.readAll(); QVERIFY(scimate::read_sci0_view(scimate::Bytes(viewBytes.begin(), viewBytes.end())).loops[0].cels.size() == 2);
        auto* split = window.findChild<QSplitter*>("previewSplitter");
        QVERIFY(split); const auto before = split->sizes(); split->setSizes({150, 400});
        QVERIFY(split->sizes() != before); split->setSizes(before);
        types->setCurrentIndex(0);
        filter->setText("pic"); QCOMPARE(table->model()->rowCount(), 1); table->selectRow(0);
        auto* planes = window.findChild<QComboBox*>("cels"); QCOMPARE(planes->count(), 3);
        planes->setCurrentIndex(2); QCOMPARE(planes->currentText(), QString("Control mask"));
        QVERIFY(details->toPlainText().contains("Mask colors"));
        window.exportSelected(temp.path() + "/mask.png", "png");
        QCOMPARE(QImage(temp.path() + "/mask.png").size(), QSize(320, 190));
        filter->setText("text"); QCOMPARE(table->model()->rowCount(), 3);
        table->selectRow(0); QVERIFY(details->toPlainText().contains("Preview unavailable"));
        window.exportSelected(temp.path() + "/text.bin", "bin");
        QFile exportedText(temp.path() + "/text.bin"); QVERIFY(exportedText.open(QIODevice::ReadOnly)); QCOMPARE(exportedText.readAll(), QByteArray("old"));
        QVERIFY(window.findChild<QPushButton*>("editText")->isHidden());
        table->selectRow(2);
        auto* strings = window.findChild<QTableWidget*>("textStrings");
        QCOMPARE(strings->rowCount(), 3); QCOMPARE(strings->item(0, 1)->text(), QString("Hello"));
        QVERIFY(window.findChild<QPushButton*>("editText")->isVisible());
        window.exportSelected(temp.path() + "/strings.tsv", "tsv");
        QFile tsv(temp.path() + "/strings.tsv"); QVERIFY(tsv.open(QIODevice::ReadOnly));
        QCOMPARE(tsv.readAll(), QByteArray("index\ttext\n0\tHello\n1\tWorld\n2\t\n"));
        window.saveSelectedText("index\ttext\n0\tHello Larry\n1\tWorld\n2\t\n");
        QCOMPARE(strings->item(0, 1)->text(), QString("Hello Larry"));
        QVERIFY(scimate::open_project(window.project()->directory).game.resources[6].payload == window.project()->game.resources[6].payload);
        filter->setText("script"); table->selectRow(1);
        window.exportSelected(temp.path() + "/script.asm", "asm");
        QFile listing(temp.path() + "/script.asm"); QVERIFY(listing.open(QIODevice::ReadOnly)); QVERIFY(listing.readAll().contains("ret"));
        project_fixture::write(window.project()->directory / "payloads/0000000.bin", {'n','e','w'});
        const auto pngPath = temp.path() + "/workspace/views/0000003/loop-0-cel-0.png";
        QImage png(pngPath); QVERIFY(!png.isNull());
        png = png.convertToFormat(QImage::Format_ARGB32); png.setPixelColor(0, 0, Qt::white);
        QVERIFY(png.save(pngPath));
        window.openProject(temp.path() + "/workspace");
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 10000);
        filter->setText("Modified"); QCOMPARE(table->model()->rowCount(), 3);
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
            filter->setText("view"); QVERIFY(table->model()->rowCount() > 0); table->selectRow(4); table->scrollTo(table->model()->index(4, 0));
        }
        const auto screenshot = qEnvironmentVariable("SCIMATE_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(100); QVERIFY(window.grab().save(screenshot)); }
    }
};
QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
