// SPDX-License-Identifier: GPL-3.0-only
// The main window with the real plugins, driven by keys (offscreen): closing
// tabs, Select Word, build profiles, debugging (with gdb and CMake, when
// installed), Back and Forward, and Go to Definition with the language server
// written for the tests (tests/lsp/fake_server.py, needs python3).
#include "DebugPanel.h"
#include "MainWindow.h"

#include <mrutabwidget.h>

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

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

QAction* actionWithKey(QWidget& window, const QKeySequence& key) {
    for (auto* action : window.findChildren<QAction*>())
        if (action->shortcut() == key) return action;
    return nullptr;
}

QAction* debugConfigureAction(QWidget& window) {
    for (auto* action : window.findChildren<QAction*>())
        if (action->text() == QLatin1String("Debug CMake &Configure")) return action;
    return nullptr;
}

// The breakpoint column of the current tab, found by clicking in it as a user
// does and looking at what the window keeps for the project.
struct BreakpointMargin {
    QWidget* rail = nullptr;
    QString project;
    QString file;
    int x = -1;
    int lineHeight = 0;

    QVariantList lines() const {
        const QString key = QStringLiteral("breakpoints/") + QString::fromLatin1(project.toUtf8().toHex());
        return QSettings().value(key).toMap().value(file).toList();
    }
    void click(int xAt, int y) { QTest::mouseClick(rail, Qt::LeftButton, Qt::NoModifier, QPoint(xAt, y)); }
    bool find(Window& w, const QString& projectDirectory, const QString& path) {
        project = QFileInfo(projectDirectory).canonicalFilePath();
        file = QFileInfo(path).canonicalFilePath();
        for (auto* child : w.tabs->currentWidget()->findChildren<QWidget*>())
            if (QLatin1String(child->metaObject()->className()) == QLatin1String("qce::LeftRail")) rail = child;
        if (!rail || !QTest::qWaitFor([this] { return rail->width() > 0; }, 5000)) return false;
        for (int at = 0; at < rail->width() && x < 0; ++at) {
            click(at, 3);
            if (!lines().isEmpty()) x = at + 1;
        }
        if (x < 0) return false;
        click(x, 3);
        for (int y = 3; y < 100 && !lineHeight; ++y) {
            click(x, y);
            if (lines().first().toInt() == 1) lineHeight = y;
            click(x, y);
        }
        return lineHeight > 0 && lines().isEmpty();
    }
    void toggle(int line) { click(x, lineHeight * line + 2); }
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

    // Each profile configures a directory of its own, and the project
    // remembers the one chosen. No compiler: a project of no language.
    void buildProfiles() {
        if (QStandardPaths::findExecutable(QStringLiteral("cmake")).isEmpty()) QSKIP("cmake is not installed");
        QTemporaryDir project;
        QFile lists(project.filePath(QStringLiteral("CMakeLists.txt")));
        QVERIFY(lists.open(QIODevice::WriteOnly));
        lists.write("cmake_minimum_required(VERSION 3.21)\nproject(p NONE)\n");
        lists.close();
        auto buildType = [&](const QString& directory) {
            QFile cache(project.filePath(directory + QStringLiteral("/CMakeCache.txt")));
            if (!cache.open(QIODevice::ReadOnly)) return QStringLiteral("no cache");
            for (const QByteArray& line : cache.readAll().split('\n'))
                if (line.startsWith("CMAKE_BUILD_TYPE:")) return QString::fromLatin1(line.mid(line.indexOf('=') + 1));
            return QString();
        };
        {
            Window w;
            QVERIFY(w.active());
            QVERIFY(w.window.openProject(project.path()));
            auto* profile = w.window.findChild<QComboBox*>(QStringLiteral("buildProfile"));
            QVERIFY(profile);
            QCOMPARE(profile->currentData().toString(), QStringLiteral("build"));
            QTRY_VERIFY_WITH_TIMEOUT(profile->isEnabled(), 20000);
            QVERIFY(QFile::exists(project.filePath(QStringLiteral("build/CMakeCache.txt"))));
            QCOMPARE(buildType(QStringLiteral("build")), QString());
            for (const char* directory : {"build-debug", "build-release"}) {
                profile->setCurrentIndex(profile->findData(QString::fromLatin1(directory)));
                emit profile->activated(profile->currentIndex());
                QVERIFY(!profile->isEnabled());
                QTRY_VERIFY_WITH_TIMEOUT(profile->isEnabled(), 20000);
            }
            QCOMPARE(buildType(QStringLiteral("build-debug")), QStringLiteral("Debug"));
            QCOMPARE(buildType(QStringLiteral("build-release")), QStringLiteral("Release"));
        }
        Window again;
        QVERIFY(again.active());
        QVERIFY(again.window.openProject(project.path()));
        QCOMPARE(again.window.findChild<QComboBox*>(QStringLiteral("buildProfile"))->currentData().toString(),
                 QStringLiteral("build-release"));
    }

    // Closing the window lets go of a configuration still going on: its end
    // would come to a window half destroyed.
    void closesWhileConfiguring() {
        if (QStandardPaths::findExecutable(QStringLiteral("cmake")).isEmpty()) QSKIP("cmake is not installed");
        QTemporaryDir project;
        QFile lists(project.filePath(QStringLiteral("CMakeLists.txt")));
        QVERIFY(lists.open(QIODevice::WriteOnly));
        lists.write("cmake_minimum_required(VERSION 3.21)\nproject(p NONE)\n");
        lists.close();
        Window w;
        QVERIFY(w.active());
        QVERIFY(w.window.openProject(project.path()));
        QVERIFY(!w.window.findChild<QComboBox*>(QStringLiteral("buildProfile"))->isEnabled());
    }

    // Breakpoints set in the margin, Debug building in build-debug, the keys,
    // the panel, and no process left when the window closes during a stop.
    void debugsAProgram() {
        for (const char* program : {"cmake", "c++", "gdb"})
            if (QStandardPaths::findExecutable(QString::fromLatin1(program)).isEmpty()) QSKIP("cmake, c++ or gdb is not installed");
        QTemporaryDir project;
        auto put = [&](const QString& name, const QByteArray& text) {
            QFile out(project.filePath(name));
            if (out.open(QIODevice::WriteOnly)) out.write(text);
            return out.fileName();
        };
        put(QStringLiteral("CMakeLists.txt"),
            "cmake_minimum_required(VERSION 3.21)\nproject(p CXX)\nadd_executable(eud_debugged main.cpp)\n");
        const QString source = put(QStringLiteral("main.cpp"),
                                   "#include <cstdio>\n"
                                   "int add(int a, int b) {\n"
                                   "    return a + b;\n"
                                   "}\n"
                                   "int main() {\n"
                                   "    std::puts(\"hello\");\n"
                                   "    int result = add(2, 3);\n"
                                   "    return result;\n"
                                   "}\n");
        auto window = std::make_unique<Window>();
        Window& w = *window;
        QVERIFY(w.active());
        QAction* debug = actionWithKey(w.window, QKeySequence(Qt::SHIFT | Qt::Key_F9));
        QAction* resume = actionWithKey(w.window, QKeySequence(Qt::Key_F9));
        QVERIFY(debug && resume);
        QVERIFY(w.window.openProject(project.path()));
        QTRY_VERIFY_WITH_TIMEOUT(debug->isEnabled(), 20000);
        QVERIFY(!resume->isEnabled());
        QVERIFY(w.window.openFile(source));
        BreakpointMargin margin;
        QVERIFY(margin.find(w, project.path(), source));
        margin.toggle(2);  // return a + b;
        QCOMPARE(margin.lines().size(), 1);
        QCOMPARE(margin.lines().first().toInt(), 2);

        QTest::keyClick(w.area(), Qt::Key_F9, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(resume->isEnabled(), 30000);
        QVERIFY(QFile::exists(project.filePath(QStringLiteral("build-debug/eud_debugged"))));
        auto* panel = w.window.findChild<eudaimonia::DebugPanel*>();
        QVERIFY(panel);
        auto* frames = panel->findChild<QListWidget*>();
        auto* variables = panel->findChild<QTreeWidget*>();
        QTRY_VERIFY(frames->count() == 2);
        QVERIFY(frames->item(0)->text().startsWith(QLatin1String("add")));
        QCOMPARE(w.tab(), QStringLiteral("main.cpp"));
        QCOMPARE(w.position(), QStringLiteral("Line 3, Column 1"));
        QTRY_VERIFY(variables->topLevelItemCount() > 0 && variables->topLevelItem(0)->childCount() == 2);
        QCOMPARE(variables->topLevelItem(0)->child(0)->text(0), QStringLiteral("a"));
        QCOMPARE(variables->topLevelItem(0)->child(0)->text(1), QStringLiteral("2"));
        QVERIFY(panel->output().contains(QLatin1String("hello")));

        // Another frame shows its place.
        frames->setCurrentRow(1);
        QTRY_COMPARE(w.position(), QStringLiteral("Line 7, Column 1"));
        // Over, then out, to main.
        QTest::keyClick(w.area(), Qt::Key_F8);
        QTRY_COMPARE(w.position(), QStringLiteral("Line 4, Column 1"));
        QTest::keyClick(w.area(), Qt::Key_F8, Qt::ShiftModifier);
        QTRY_VERIFY(resume->isEnabled() && frames->count() == 1 && frames->item(0)->text().startsWith(QLatin1String("main")));
        // On to the end.
        QTest::keyClick(w.area(), Qt::Key_F9);
        QTRY_VERIFY_WITH_TIMEOUT(debug->isEnabled(), 10000);
        QVERIFY(!resume->isEnabled());

        QTest::keyClick(w.area(), Qt::Key_F9, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(resume->isEnabled(), 30000);
        window.reset();
        QTRY_VERIFY_WITH_TIMEOUT(QProcess::execute(QStringLiteral("pgrep"), {QStringLiteral("-x"), QStringLiteral("eud_debugged")}) != 0, 8000);
    }

    // A breakpoint kept from the window before stops CMake configuring.
    void debugsCMake() {
        if (QStandardPaths::findExecutable(QStringLiteral("cmake")).isEmpty()) QSKIP("cmake is not installed");
        QTemporaryDir project;
        QFile lists(project.filePath(QStringLiteral("CMakeLists.txt")));
        QVERIFY(lists.open(QIODevice::WriteOnly));
        lists.write("cmake_minimum_required(VERSION 3.21)\nproject(p NONE)\nset(ITEMS a b)\nmessage(STATUS \"said\")\n");
        lists.close();
        {
            Window w;
            QVERIFY(w.active());
            QVERIFY(w.window.openProject(project.path()));
            QVERIFY(w.window.openFile(lists.fileName()));
            BreakpointMargin margin;
            QVERIFY(margin.find(w, project.path(), lists.fileName()));
            margin.toggle(3);
            QCOMPARE(margin.lines().size(), 1);
        }
        Window w;
        QVERIFY(w.active());
        QVERIFY(w.window.openProject(project.path()));
        QAction* configure = debugConfigureAction(w.window);
        QAction* resume = actionWithKey(w.window, QKeySequence(Qt::Key_F9));
        QVERIFY(configure && resume);
        QTRY_VERIFY_WITH_TIMEOUT(configure->isEnabled(), 20000);
        configure->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(resume->isEnabled(), 15000);
        QTRY_COMPARE(w.position(), QStringLiteral("Line 4, Column 1"));
        QCOMPARE(w.tab(), QStringLiteral("CMakeLists.txt"));
        resume->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(configure->isEnabled(), 15000);
        QVERIFY(w.window.findChild<eudaimonia::DebugPanel*>()->output().contains(QLatin1String("said")));
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
