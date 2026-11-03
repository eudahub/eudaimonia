// SPDX-License-Identifier: GPL-3.0-only
// Debugging through the Debug Adapter Protocol: programs with gdb (gdb -i dap,
// gdb 14 or later) and CMake's scripts with CMake's own debugger
// (cmake --debugger, CMake 3.27 or later).

#include "DapSession.h"

#include <eudaimonia/plugin/Debugger.h>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace eudaimonia {
namespace {

// Long: gdb reads the symbols of a big program first.
constexpr int startTimeoutMs = 60000;

bool isExecutableProgram(const QString& path) {
    if (!QFileInfo(path).isExecutable()) return false;
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.read(4) == QByteArray("\x7f" "ELF");
}

// What rust-gdb gives gdb, so that a Vec or a String shows its contents: the
// place of the pretty printers a Rust program asks gdb to load. None without
// rustc; other programs ask for nothing from there.
QStringList rustPrinterArguments() {
    static const QStringList arguments = [] {
        QProcess rustc;
        rustc.start(QStringLiteral("rustc"), {QStringLiteral("--print"), QStringLiteral("sysroot")});
        if (!rustc.waitForFinished(5000) || rustc.exitCode() != 0) return QStringList();
        const QString printers = QString::fromLocal8Bit(rustc.readAllStandardOutput()).trimmed() + QStringLiteral("/lib/rustlib/etc");
        if (!QFileInfo(printers).isDir()) return QStringList();
        return QStringList{QStringLiteral("--directory=") + printers, QStringLiteral("-iex"),
                           QStringLiteral("add-auto-load-safe-path ") + printers};
    }();
    return arguments;
}

} // namespace

class DapPlugin : public QObject, public Debugger {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EUDAIMONIA_DEBUGGER_IID)
    Q_INTERFACES(eudaimonia::Debugger)

public:
    ~DapPlugin() override { delete m_session; }

    QString unavailableReason(DebugRequest::Kind kind) const override {
        const bool cmake = kind == DebugRequest::Kind::CMakeConfigure;
        const QString program = cmake ? QStringLiteral("cmake") : QStringLiteral("gdb");
        if (!QStandardPaths::findExecutable(program).isEmpty()) return {};
        return tr("%1 is not installed. Install it with: sudo apt install %1").arg(program);
    }

    void setListener(DebuggerListener* listener) override { m_listener = listener; }

    void setBreakpoints(const QString& file, const QList<int>& lines) override {
        if (lines.isEmpty()) m_breakpoints.remove(file);
        else m_breakpoints.insert(file, lines);
        if (m_session && m_configured) sendBreakpoints(file, lines);
    }

    bool start(const DebugRequest& request) override {
        if (m_session) {
            if (m_listener) m_listener->ended(-1, tr("A debugging session is running already."));
            return false;
        }
        const QString reason = unavailableReason(request.kind);
        if (!reason.isEmpty()) {
            if (m_listener) m_listener->ended(-1, reason);
            return false;
        }
        // gdb never answers about a program it cannot load, e.g. a script.
        if (request.kind == DebugRequest::Kind::Program && !isExecutableProgram(request.program)) {
            if (m_listener) m_listener->ended(-1, tr("%1 is not a compiled program.").arg(request.program));
            return false;
        }
        m_request = request;
        m_exitCode = -1;
        m_threadId = 0;
        m_configured = false;
        m_started = false;
        m_breakpointsById.clear();
        m_error.clear();
        m_session = new dap::DapSession(this);
        connect(m_session, &dap::DapSession::connected, this, &DapPlugin::initialize);
        connect(m_session, &dap::DapSession::event, this, &DapPlugin::onEvent);
        connect(m_session, &dap::DapSession::output, this, [this](const QString& text) {
            if (m_listener) m_listener->output(text);
        });
        connect(m_session, &dap::DapSession::ended, this, &DapPlugin::onEnded);
        if (request.kind == DebugRequest::Kind::CMakeConfigure) {
            m_session->startOverSocket(QStringLiteral("cmake"), request.arguments, request.workingDirectory,
                                       {QStringLiteral("--debugger"), QStringLiteral("--debugger-pipe")});
        } else {
            // The program named at once: its symbols are known when the
            // breakpoints come, which then need not wait.
            const QStringList gdb = QStringList{QStringLiteral("gdb"), QStringLiteral("-i"), QStringLiteral("dap"), QStringLiteral("-q")}
                                    + rustPrinterArguments() + QStringList{request.program};
            // C's stdio holds back what a program writes into a pipe; stdbuf
            // (when installed), which the program inherits from gdb, makes it
            // come line by line, as one steps through it.
            if (QStandardPaths::findExecutable(QStringLiteral("stdbuf")).isEmpty())
                m_session->startOverStdio(gdb.first(), gdb.mid(1), request.workingDirectory);
            else
                m_session->startOverStdio(QStringLiteral("stdbuf"),
                                          QStringList{QStringLiteral("-oL"), QStringLiteral("-eL")} + gdb,
                                          request.workingDirectory);
        }
        // An adapter that says nothing (gdb, given a file it cannot load)
        // does not keep the session for ever.
        QTimer::singleShot(startTimeoutMs, m_session, [this] {
            if (!m_started) fail(tr("The debugger did not start: %1 cannot be debugged.").arg(m_request.program));
        });
        return true;
    }

    bool isRunning() const override { return m_session != nullptr; }

    void stop() override {
        if (m_session) m_session->stop({{QStringLiteral("terminateDebuggee"), true}});
    }

    void resume() override { step(QStringLiteral("continue")); }
    void stepOver() override { step(QStringLiteral("next")); }
    void stepIn() override { step(QStringLiteral("stepIn")); }
    void stepOut() override { step(QStringLiteral("stepOut")); }

    void pause() override {
        if (m_session) m_session->request(QStringLiteral("pause"), {{QStringLiteral("threadId"), m_threadId}});
    }

    void stackTrace(int threadId, std::function<void(const QList<DebugFrame>&)> done) override {
        if (!m_session) {
            done({});
            return;
        }
        m_session->request(QStringLiteral("stackTrace"), {{QStringLiteral("threadId"), threadId}},
                           [done](bool, const QJsonObject& body, const QString&) {
                               QList<DebugFrame> frames;
                               for (const QJsonValue& value : body.value(QStringLiteral("stackFrames")).toArray()) {
                                   const QJsonObject frame = value.toObject();
                                   frames << DebugFrame{frame.value(QStringLiteral("id")).toInt(),
                                                        frame.value(QStringLiteral("name")).toString(),
                                                        frame.value(QStringLiteral("source")).toObject()
                                                            .value(QStringLiteral("path")).toString(),
                                                        frame.value(QStringLiteral("line")).toInt()};
                               }
                               done(frames);
                           });
    }

    void scopes(int frameId, std::function<void(const QList<DebugScope>&)> done) override {
        if (!m_session) {
            done({});
            return;
        }
        m_session->request(QStringLiteral("scopes"), {{QStringLiteral("frameId"), frameId}},
                           [done](bool, const QJsonObject& body, const QString&) {
                               QList<DebugScope> result;
                               for (const QJsonValue& value : body.value(QStringLiteral("scopes")).toArray()) {
                                   const QJsonObject scope = value.toObject();
                                   result << DebugScope{scope.value(QStringLiteral("name")).toString(),
                                                        scope.value(QStringLiteral("variablesReference")).toInt()};
                               }
                               done(result);
                           });
    }

    void variables(int variablesReference, std::function<void(const QList<DebugVariable>&)> done) override {
        if (!m_session) {
            done({});
            return;
        }
        m_session->request(QStringLiteral("variables"), {{QStringLiteral("variablesReference"), variablesReference}},
                           [done](bool, const QJsonObject& body, const QString&) {
                               QList<DebugVariable> result;
                               for (const QJsonValue& value : body.value(QStringLiteral("variables")).toArray()) {
                                   const QJsonObject variable = value.toObject();
                                   result << DebugVariable{variable.value(QStringLiteral("name")).toString(),
                                                           variable.value(QStringLiteral("value")).toString(),
                                                           variable.value(QStringLiteral("type")).toString(),
                                                           variable.value(QStringLiteral("variablesReference")).toInt()};
                               }
                               done(result);
                           });
    }

private:
    void initialize() {
        const QString adapter = m_request.kind == DebugRequest::Kind::CMakeConfigure ? QStringLiteral("cmake") : QStringLiteral("gdb");
        m_session->request(QStringLiteral("initialize"),
                           {{QStringLiteral("clientID"), QStringLiteral("eudaimonia")},
                            {QStringLiteral("adapterID"), adapter},
                            {QStringLiteral("linesStartAt1"), true},
                            {QStringLiteral("columnsStartAt1"), true},
                            {QStringLiteral("pathFormat"), QStringLiteral("path")}},
                           [this](bool success, const QJsonObject&, const QString& message) {
                               if (!success) {
                                   fail(message);
                                   return;
                               }
                               // CMake runs by itself; gdb is told what to run,
                               // and answers once configured.
                               if (m_request.kind == DebugRequest::Kind::Program) launch();
                           });
    }

    void launch() {
        QJsonArray arguments;
        for (const QString& argument : m_request.arguments) arguments << argument;
        m_session->request(QStringLiteral("launch"),
                           {{QStringLiteral("program"), m_request.program},
                            {QStringLiteral("args"), arguments},
                            {QStringLiteral("cwd"), m_request.workingDirectory}},
                           [this](bool success, const QJsonObject&, const QString& message) {
                               m_started = true;
                               if (!success) fail(message);
                           });
    }

    // The session goes on only to end.
    void fail(const QString& message) {
        m_error = message;
        stop();
    }

    void step(const QString& command) {
        if (!m_session) return;
        m_session->request(command, {{QStringLiteral("threadId"), m_threadId}});
        if (m_listener) m_listener->continued();
    }

    void sendBreakpoints(const QString& file, const QList<int>& lines) {
        QJsonArray requested;
        for (const int line : lines) requested << QJsonObject{{QStringLiteral("line"), line}};
        m_session->request(QStringLiteral("setBreakpoints"),
                           {{QStringLiteral("source"), QJsonObject{{QStringLiteral("path"), file}}},
                            {QStringLiteral("breakpoints"), requested}},
                           [this, file, lines](bool, const QJsonObject& body, const QString&) {
                               const QJsonArray taken = body.value(QStringLiteral("breakpoints")).toArray();
                               for (qsizetype i = 0; i < taken.size() && i < lines.size(); ++i) {
                                   const QJsonObject breakpoint = taken.at(i).toObject();
                                   DebugBreakpoint known{file, lines.at(i), breakpoint.value(QStringLiteral("line")).toInt(lines.at(i)),
                                                         breakpoint.value(QStringLiteral("verified")).toBool()};
                                   m_breakpointsById.insert(breakpoint.value(QStringLiteral("id")).toInt(), known);
                                   if (m_listener) m_listener->breakpointChanged(known);
                               }
                           });
    }

    void onEvent(const QString& name, const QJsonObject& body) {
        if (name == QLatin1String("initialized")) {
            // The breakpoints go before the debuggee runs.
            for (auto it = m_breakpoints.cbegin(); it != m_breakpoints.cend(); ++it) sendBreakpoints(it.key(), it.value());
            m_configured = true;
            m_session->request(QStringLiteral("configurationDone"), {},
                               [this](bool, const QJsonObject&, const QString&) {
                                   // CMake has no launch to answer.
                                   if (m_request.kind == DebugRequest::Kind::CMakeConfigure) m_started = true;
                               });
        } else if (name == QLatin1String("stopped")) {
            m_threadId = body.value(QStringLiteral("threadId")).toInt(m_threadId);
            if (m_listener) m_listener->stopped(m_threadId, body.value(QStringLiteral("reason")).toString());
        } else if (name == QLatin1String("thread")) {
            if (m_threadId == 0) m_threadId = body.value(QStringLiteral("threadId")).toInt();
        } else if (name == QLatin1String("output")) {
            if (m_listener) m_listener->output(body.value(QStringLiteral("output")).toString());
        } else if (name == QLatin1String("breakpoint")) {
            const QJsonObject breakpoint = body.value(QStringLiteral("breakpoint")).toObject();
            const int id = breakpoint.value(QStringLiteral("id")).toInt();
            if (!m_breakpointsById.contains(id)) return;
            DebugBreakpoint& known = m_breakpointsById[id];
            known.line = breakpoint.value(QStringLiteral("line")).toInt(known.line);
            known.verified = breakpoint.value(QStringLiteral("verified")).toBool();
            if (m_listener) m_listener->breakpointChanged(known);
        } else if (name == QLatin1String("exited")) {
            m_exitCode = body.value(QStringLiteral("exitCode")).toInt(-1);
        } else if (name == QLatin1String("terminated")) {
            m_session->debuggeeEnded();
        }
    }

    void onEnded(const QString& error) {
        dap::DapSession* session = m_session;
        m_session = nullptr;
        session->deleteLater();
        if (m_listener) m_listener->ended(m_exitCode, m_error.isEmpty() ? error : m_error);
    }

    DebuggerListener* m_listener = nullptr;
    dap::DapSession* m_session = nullptr;
    DebugRequest m_request;
    QHash<QString, QList<int>> m_breakpoints;       // by file; kept between sessions
    QHash<int, DebugBreakpoint> m_breakpointsById;  // as the adapter took them
    bool m_configured = false;
    bool m_started = false;  // the adapter answered that the debuggee runs
    int m_threadId = 0;
    int m_exitCode = -1;
    QString m_error;
};

} // namespace eudaimonia

#include "DapPlugin.moc"
