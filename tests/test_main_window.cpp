// SPDX-License-Identifier: GPL-3.0-only
// The main window with the real plugins, driven by keys (offscreen): closing
// tabs, Select Word, Back and Forward, and Go to Definition with the language
// server written for the tests (tests/lsp/fake_server.py, needs python3).
#include "MainWindow.h"

#include <mrutabwidget.h>

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

namespace {

// A window with files open, its keys going to the current editor.
struct Window {
    eudaimonia::MainWindow window;
    MruTabWidget* tabs = nullptr;

    Window() {
        window.show();
        tabs = window.findChild<MruTabWidget*>();
    }
    bool active() { return QTest::qWaitForWindowActive(&window) && tabs; }
    // The current editor's text area, focused.
    QAbstractScrollArea* area() {
        auto* found = tabs->currentWidget()->findChild<QAbstractScrollArea*>();
        found->setFocus();
        return found;
    }
    QString position() const {
        for (auto* label : window.statusBar()->findChildren<QLabel*>())
            if (label->text().startsWith(QLatin1String("Line "))) return label->text().section(QStringLiteral("   "), 0, 0);
        return {};
    }
    QString tab() const { return tabs->tabText(tabs->currentIndex()); }
};

// Answers the message box the next action brings up.
void answer(QMessageBox::StandardButton button) {
    QTimer::singleShot(50, qApp, [button] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) box->button(button)->click();
    });
}

} // namespace

class TestMainWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString file(const QString& name, const QByteArray& text) {
        const QString path = m_dir.filePath(name);
        QFile out(path);
        if (out.open(QIODevice::WriteOnly)) out.write(text);
        return path;
    }
    QByteArray read(const QString& path) {
        QFile in(path);
        return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void tabsHaveCloseButtonsAndCtrlF4() {
        Window w;
        QVERIFY(w.active());
        QVERIFY(w.window.openFile(file("a.txt", "a\n")));
        QVERIFY(w.window.openFile(file("b.txt", "b\n")));
        QCOMPARE(w.tabs->count(), 2);
        auto* button = w.tabs->tabBar()->tabButton(w.tabs->currentIndex(), QTabBar::RightSide);
        QVERIFY(button);
        QVERIFY(button->isVisible());
        QTest::keyClick(w.area(), Qt::Key_F4, Qt::ControlModifier);
        QTRY_COMPARE(w.tabs->count(), 1);
    }

    void closingAModifiedTabAsks() {
        const QString path = file("modified.txt", "text\n");
        Window w;
        QVERIFY(w.active());
        QVERIFY(w.window.openFile(path));
        QTest::keyClick(w.area(), Qt::Key_X);

        answer(QMessageBox::Cancel);
        QTest::keyClick(w.area(), Qt::Key_F4, Qt::ControlModifier);
        QCOMPARE(w.tabs->count(), 1);

        // The window manager a desktop has gives the window back the focus
        // when the question goes; the offscreen platform has none.
        w.window.activateWindow();
        QVERIFY(w.active());
        answer(QMessageBox::Discard);
        QTest::keyClick(w.area(), Qt::Key_F4, Qt::ControlModifier);
        QTRY_COMPARE(w.tabs->count(), 0);
        QCOMPARE(read(path), QByteArray("text\n"));
    }

    // Ctrl+W selects the word (typing replaces it) and closes nothing.
    void ctrlWSelectsTheWord() {
        const QString path = file("word.txt", "alpha zażółć_1 beta\n");
        Window w;
        QVERIFY(w.active());
        QVERIFY(w.window.openFile(path));
        auto* area = w.area();
        for (int i = 0; i < 8; ++i) QTest::keyClick(area, Qt::Key_Right);
        QTest::keyClick(area, Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(w.tabs->count(), 1);
        QTest::keyClick(area, Qt::Key_X);
        QTest::keyClick(area, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(read(path), QByteArray("alpha x beta\n"));
    }

    void backAndForwardAcrossFiles() {
        Window w;
        QVERIFY(w.active());
        QVERIFY(w.window.openFile(file("first.txt", "1\n")));
        QVERIFY(w.window.openFile(file("second.txt", "2\n")));
        QCOMPARE(w.tab(), QStringLiteral("second.txt"));
        QTest::keyClick(w.area(), Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(w.tab(), QStringLiteral("first.txt"));
        QTest::keyClick(w.area(), Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(w.tab(), QStringLiteral("second.txt"));
    }

    // F12 opens what the server answers (line 0, column 4); Back returns.
    void goesToTheDefinitionAndBack() {
        if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) QSKIP("python3 is not installed");
        QTemporaryDir project;
        const QString path = project.filePath(QStringLiteral("a.fake"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("head\nsecond\nthird\n");
        out.close();
        Window w;
        QVERIFY(w.active());
        QVERIFY(w.window.openProject(project.path()));
        QVERIFY(w.window.openFile(path));
        auto* area = w.area();
        QTest::keyClick(area, Qt::Key_Down);
        QTest::keyClick(area, Qt::Key_Down);
        QCOMPARE(w.position(), QStringLiteral("Line 3, Column 1"));
        QTest::keyClick(area, Qt::Key_F12);
        QTRY_COMPARE(w.position(), QStringLiteral("Line 1, Column 5"));
        QTest::keyClick(area, Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(w.position(), QStringLiteral("Line 3, Column 1"));
    }
};

int main(int argc, char** argv) {
    // Settings, data and the language servers' list of the test's own, set
    // before anything reads them: nothing of the user's is touched.
    QTemporaryDir directory;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("XDG_CONFIG_HOME", directory.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_DATA_HOME", directory.filePath(QStringLiteral("data")).toUtf8());
    const QJsonArray servers{QJsonObject{
        {"name", "fake"}, {"languages", "Fake"}, {"program", "python3"},
        {"arguments", QJsonArray{QStringLiteral(FAKE_SERVER), directory.filePath(QStringLiteral("server.log"))}},
        {"suffixes", QJsonArray{"fake"}}, {"languageId", "fake"}}};
    const QString config = directory.filePath(QStringLiteral("servers.json"));
    QFile list(config);
    if (list.open(QIODevice::WriteOnly)) list.write(QJsonDocument(servers).toJson());
    list.close();
    qputenv("EUDAIMONIA_LSP_SERVERS", config.toUtf8());

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("eudaimonia-test"));
    QCoreApplication::setApplicationName(QStringLiteral("test_main_window"));
    TestMainWindow test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_main_window.moc"
