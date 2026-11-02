// SPDX-License-Identifier: GPL-3.0-only
// The debugger plugin, loaded from its file, on real debuggers: gdb on the
// programs of tests/dap, CMake on small projects written here. A debugger not
// installed skips its cases. A second or two in all.
#include <eudaimonia/plugin/Debugger.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPluginLoader>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace eudaimonia;

namespace {

constexpr int timeoutMs = 15000;

struct Listener : DebuggerListener {
    int stops = 0;
    int thread = 0;
    QString reason;
    QString written;
    bool over = false;
    int exitCode = -99;
    QString error;
    QList<DebugBreakpoint> breakpoints;

    void stopped(int threadId, const QString& why) override {
        ++stops;
        thread = threadId;
        reason = why;
    }
    void output(const QString& text) override { written += text; }
    void breakpointChanged(const DebugBreakpoint& breakpoint) override { breakpoints << breakpoint; }
    void ended(int code, const QString& why) override {
        over = true;
        exitCode = code;
        error = why;
    }
};

bool running(const QString& name) {
    QProcess pgrep;
    pgrep.start(QStringLiteral("pgrep"), {QStringLiteral("-x"), name});
    pgrep.waitForFinished();
    return pgrep.exitCode() == 0;
}

} // namespace

class TestDapPlugin : public QObject {
    Q_OBJECT

private:
    Debugger* m_debugger = nullptr;
    Listener m_listener;
    QTemporaryDir m_dir;
    const QString m_source = QStringLiteral(DAP_PROGRAM_SOURCE);
    const QString m_program = QStringLiteral(DAP_PROGRAM);
    const QString m_loop = QStringLiteral(DAP_LOOP);

    template <typename Result, typename Call>
    Result answer(Call call) {
        Result result;
        bool answered = false;
        call([&](const Result& value) {
            result = value;
            answered = true;
        });
        // The callers go on to read the answer, so waiting in vain ends here.
        if (!QTest::qWaitFor([&] { return answered; }, timeoutMs)) qFatal("The debugger did not answer");
        return result;
    }
    QList<DebugFrame> stack() {
        return answer<QList<DebugFrame>>([this](auto done) { m_debugger->stackTrace(m_listener.thread, done); });
    }
    QList<DebugScope> scopes(int frame) {
        return answer<QList<DebugScope>>([=, this](auto done) { m_debugger->scopes(frame, done); });
    }
    QList<DebugVariable> variables(int reference) {
        return answer<QList<DebugVariable>>([=, this](auto done) { m_debugger->variables(reference, done); });
    }
    bool stoppedTimes(int stops) {
        return QTest::qWaitFor([&] { return m_listener.stops == stops; }, timeoutMs);
    }
    bool ended() {
        return QTest::qWaitFor([&] { return m_listener.over; }, timeoutMs);
    }
    // A CMake project with this CMakeLists.txt; returns its directory.
    QString project(const QString& name, const QByteArray& lists) {
        QDir(m_dir.path()).mkpath(name);
        QFile file(m_dir.filePath(name + QStringLiteral("/CMakeLists.txt")));
        file.open(QIODevice::WriteOnly) && file.write(lists);
        return m_dir.filePath(name);
    }
    DebugRequest configure(const QString& directory) {
        return {DebugRequest::Kind::CMakeConfigure, QStringLiteral("cmake"),
                {QStringLiteral("-S"), directory, QStringLiteral("-B"), directory + QStringLiteral("/build")},
                directory};
    }
    DebugRequest program(const QString& path, const QStringList& arguments = {}) {
        return {DebugRequest::Kind::Program, path, arguments, QFileInfo(path).absolutePath()};
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        static QPluginLoader loader(QStringLiteral(DAP_PLUGIN_FILE));
        m_debugger = qobject_cast<Debugger*>(loader.instance());
        QVERIFY2(m_debugger, qPrintable(loader.errorString()));
        m_debugger->setListener(&m_listener);
    }

    void init() {
        m_listener = Listener();
        QVERIFY(!m_debugger->isRunning());
    }

    void debugsAProgram() {
        if (!m_debugger->unavailableReason(DebugRequest::Kind::Program).isEmpty()) QSKIP("gdb is not installed");
        m_debugger->setBreakpoints(m_source, {6, 14});
        QVERIFY(m_debugger->start(program(m_program, {QStringLiteral("one")})));
        QVERIFY(stoppedTimes(1));
        QCOMPARE(m_listener.reason, QStringLiteral("breakpoint"));
        QList<DebugFrame> frames = stack();
        QCOMPARE(frames.first().name, QStringLiteral("main"));
        QCOMPARE(frames.first().line, 14);
        QCOMPARE(frames.first().file, m_source);
        QVERIFY(m_listener.written.contains(QStringLiteral("printed 2")));

        m_debugger->resume();
        QVERIFY(stoppedTimes(2));
        frames = stack();
        QCOMPARE(frames.first().name, QStringLiteral("add"));
        QCOMPARE(frames.size(), 2);
        const QList<DebugVariable> arguments = variables(scopes(frames.first().id).first().variablesReference);
        QCOMPARE(arguments.size(), 2);
        QCOMPARE(arguments.at(0).name, QStringLiteral("a"));
        QCOMPARE(arguments.at(0).value, QStringLiteral("2"));

        m_debugger->stepOver();
        QVERIFY(stoppedTimes(3));
        QCOMPARE(m_listener.reason, QStringLiteral("step"));
        QCOMPARE(stack().first().line, 8);
        m_debugger->stepOut();
        QVERIFY(stoppedTimes(4));
        QCOMPARE(stack().first().name, QStringLiteral("main"));

        m_debugger->resume();
        QVERIFY(ended());
        QCOMPARE(m_listener.exitCode, 5);
        QVERIFY2(m_listener.error.isEmpty(), qPrintable(m_listener.error));
        QVERIFY(!m_debugger->isRunning());
        bool verified = false;
        for (const DebugBreakpoint& breakpoint : std::as_const(m_listener.breakpoints))
            if (breakpoint.file == m_source && breakpoint.line == 6) verified = breakpoint.verified;
        QVERIFY(verified);
        m_debugger->setBreakpoints(m_source, {});
    }

    // A breakpoint on a line without code stops on the next line with some.
    void movesABreakpointToCode() {
        if (!m_debugger->unavailableReason(DebugRequest::Kind::Program).isEmpty()) QSKIP("gdb is not installed");
        m_debugger->setBreakpoints(m_source, {7});
        QVERIFY(m_debugger->start(program(m_program)));
        QVERIFY(stoppedTimes(1));
        QCOMPARE(stack().first().line, 8);
        QVERIFY(!m_listener.breakpoints.isEmpty());
        QCOMPARE(m_listener.breakpoints.first().requestedLine, 7);
        QCOMPARE(m_listener.breakpoints.last().line, 8);
        m_debugger->stop();
        QVERIFY(ended());
        m_debugger->setBreakpoints(m_source, {});
    }

    void pausesAndStops() {
        if (!m_debugger->unavailableReason(DebugRequest::Kind::Program).isEmpty()) QSKIP("gdb is not installed");
        const QString name = QFileInfo(m_loop).fileName();
        QVERIFY(m_debugger->start(program(m_loop)));
        QTRY_VERIFY_WITH_TIMEOUT(running(name), timeoutMs);
        m_debugger->pause();
        QVERIFY(stoppedTimes(1));
        QCOMPARE(m_listener.reason, QStringLiteral("pause"));
        m_debugger->stop();
        QVERIFY(ended());
        QTRY_VERIFY_WITH_TIMEOUT(!running(name), timeoutMs);
    }

    void stopsARunningProgram() {
        if (!m_debugger->unavailableReason(DebugRequest::Kind::Program).isEmpty()) QSKIP("gdb is not installed");
        const QString name = QFileInfo(m_loop).fileName();
        QVERIFY(m_debugger->start(program(m_loop)));
        QTRY_VERIFY_WITH_TIMEOUT(running(name), timeoutMs);
        m_debugger->stop();
        QVERIFY(ended());
        QTRY_VERIFY_WITH_TIMEOUT(!running(name), timeoutMs);
    }

    void oneSessionAtATime() {
        if (!m_debugger->unavailableReason(DebugRequest::Kind::Program).isEmpty()) QSKIP("gdb is not installed");
        QVERIFY(m_debugger->start(program(m_loop)));
        QVERIFY(!m_debugger->start(program(m_program)));
        QVERIFY(m_listener.over);  // told why
        m_listener.over = false;
        m_debugger->stop();
        QVERIFY(ended());
    }

    // gdb would never answer about these.
    void refusesWhatIsNoProgram() {
        QVERIFY(!m_debugger->start(program(m_dir.filePath(QStringLiteral("missing")))));
        QVERIFY(m_listener.over);
        QVERIFY(!m_listener.error.isEmpty());

        const QString script = m_dir.filePath(QStringLiteral("script.sh"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\necho x\n");
        file.close();
        file.setPermissions(file.permissions() | QFile::ExeOwner);
        m_listener = Listener();
        QVERIFY(!m_debugger->start(program(script)));
        QVERIFY(m_listener.over);
        QVERIFY(!m_debugger->isRunning());
    }

    void debugsACMakeScript() {
        if (!m_debugger->unavailableReason(DebugRequest::Kind::CMakeConfigure).isEmpty()) QSKIP("cmake is not installed");
        const QString directory = project(QStringLiteral("script"),
                                          "cmake_minimum_required(VERSION 3.21)\n"
                                          "project(s NONE)\n"
                                          "set(ITEMS a b c)\n"
                                          "message(STATUS \"hi\")\n"
                                          "set(LAST done)\n");
        const QString lists = directory + QStringLiteral("/CMakeLists.txt");
        m_debugger->setBreakpoints(lists, {4});
        QVERIFY(m_debugger->start(configure(directory)));
        QVERIFY(stoppedTimes(1));
        const QList<DebugFrame> frames = stack();
        QCOMPARE(frames.first().line, 4);

        // The values of lists, among the variables of the directory.
        QString items;
        for (const DebugVariable& group : variables(scopes(frames.first().id).first().variablesReference)) {
            if (group.name != QLatin1String("Locals")) continue;
            for (const DebugVariable& variable : variables(group.variablesReference))
                if (variable.name == QLatin1String("ITEMS")) items = variable.value;
        }
        QCOMPARE(items, QStringLiteral("a;b;c"));

        m_debugger->stepOver();
        QVERIFY(stoppedTimes(2));
        QCOMPARE(stack().first().line, 5);
        m_debugger->resume();
        QVERIFY(ended());
        QCOMPARE(m_listener.exitCode, 0);
        QVERIFY2(m_listener.error.isEmpty(), qPrintable(m_listener.error));
        QVERIFY(m_listener.written.contains(QStringLiteral("-- hi")));
        // It went on to generate after its script.
        QVERIFY(QFile::exists(directory + QStringLiteral("/build/cmake_install.cmake")));
        m_debugger->setBreakpoints(lists, {});
    }

    void stopsAtACMakeError() {
        if (!m_debugger->unavailableReason(DebugRequest::Kind::CMakeConfigure).isEmpty()) QSKIP("cmake is not installed");
        const QString directory = project(QStringLiteral("broken"),
                                          "cmake_minimum_required(VERSION 3.21)\n"
                                          "project(s NONE)\n"
                                          "message(FATAL_ERROR \"broken\")\n");
        QVERIFY(m_debugger->start(configure(directory)));
        QVERIFY(stoppedTimes(1));
        QCOMPARE(m_listener.reason, QStringLiteral("exception"));
        QCOMPARE(stack().first().line, 3);
        m_debugger->resume();
        QVERIFY(ended());
        QVERIFY(m_listener.exitCode != 0);
        QVERIFY(m_listener.written.contains(QStringLiteral("broken")));
    }

    void stopsCMake() {
        if (!m_debugger->unavailableReason(DebugRequest::Kind::CMakeConfigure).isEmpty()) QSKIP("cmake is not installed");
        const QString directory = project(QStringLiteral("stopped"),
                                          "cmake_minimum_required(VERSION 3.21)\n"
                                          "project(s NONE)\n"
                                          "message(STATUS \"here\")\n");
        const QString lists = directory + QStringLiteral("/CMakeLists.txt");
        m_debugger->setBreakpoints(lists, {3});
        QVERIFY(m_debugger->start(configure(directory)));
        QVERIFY(stoppedTimes(1));
        m_debugger->stop();
        QVERIFY(ended());
        m_debugger->setBreakpoints(lists, {});
    }
};

QTEST_GUILESS_MAIN(TestDapPlugin)
#include "test_dap_plugin.moc"
