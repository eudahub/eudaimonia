// SPDX-License-Identifier: GPL-3.0-only
#ifndef EUDAIMONIA_PLUGIN_DEBUGGER_H
#define EUDAIMONIA_PLUGIN_DEBUGGER_H

// Interface of the debugger plugin: debugging a program or a build system's
// script, with breakpoints, stepping, the call stack and variables. Used on
// the GUI thread; what the debugger does comes later, through the listener
// and callbacks. One session at a time. Lines count from 1.

#include <QList>
#include <QString>
#include <QStringList>
#include <QtPlugin>

#include <functional>

namespace eudaimonia {

// What to debug.
struct DebugRequest {
    enum class Kind {
        Program,        // `program` with `arguments`, built with debug information
        CMakeConfigure  // CMake configuring a project: `arguments` as a
                        // BuildSystem's configureCommand() gives them
    };
    Kind kind = Kind::Program;
    QString program;
    QStringList arguments;
    QString workingDirectory;
};

// A breakpoint as the debugger took it.
struct DebugBreakpoint {
    QString file;
    int requestedLine = 0;
    int line = 0;           // where it really is, e.g. the next line with code
    bool verified = false;  // false: not in effect yet (a library not loaded)
};

struct DebugFrame {
    int id = 0;
    QString name;
    QString file;  // empty without source
    int line = 0;
};

// A group of variables of a frame, e.g. "Locals".
struct DebugScope {
    QString name;
    int variablesReference = 0;
};

struct DebugVariable {
    QString name;
    QString value;
    QString type;
    // Non-zero: it has members, asked for by variables().
    int variablesReference = 0;
};

// Told what the debugger does; the IDE implements what it needs.
class DebuggerListener {
public:
    virtual ~DebuggerListener() = default;
    // The debuggee stopped (`reason`: "breakpoint", "step", "pause", ...):
    // stack, scopes and variables can be asked for now.
    virtual void stopped(int threadId, const QString& reason) { (void)threadId; (void)reason; }
    // It runs again after a stop.
    virtual void continued() {}
    // What the debuggee or the debugger wrote.
    virtual void output(const QString& text) { (void)text; }
    virtual void breakpointChanged(const DebugBreakpoint& breakpoint) { (void)breakpoint; }
    // The session is over: the debuggee's exit code (-1 when unknown), and
    // why when it failed.
    virtual void ended(int exitCode, const QString& error) { (void)exitCode; (void)error; }
};

class Debugger {
public:
    virtual ~Debugger() = default;

    // Why this kind cannot be debugged here, with what to do about it (e.g.
    // gdb not installed); empty when it can.
    virtual QString unavailableReason(DebugRequest::Kind kind) const = 0;

    // Not owned; nullptr removes it.
    virtual void setListener(DebuggerListener* listener) = 0;

    // The breakpoints of a file, replacing those set before, for this session
    // and the following ones; an empty list clears them.
    virtual void setBreakpoints(const QString& file, const QList<int>& lines) = 0;

    // Starts a session; false, telling through ended(), when one runs already
    // or it cannot start.
    virtual bool start(const DebugRequest& request) = 0;
    virtual bool isRunning() const = 0;
    // Ends the session and the debuggee.
    virtual void stop() = 0;

    // After a stop, in the thread that stopped.
    virtual void resume() = 0;
    virtual void stepOver() = 0;
    virtual void stepIn() = 0;
    virtual void stepOut() = 0;
    // While it runs.
    virtual void pause() = 0;

    // After a stop; each callback gets nothing when the debugger has no answer.
    virtual void stackTrace(int threadId, std::function<void(const QList<DebugFrame>&)> done) = 0;
    virtual void scopes(int frameId, std::function<void(const QList<DebugScope>&)> done) = 0;
    virtual void variables(int variablesReference, std::function<void(const QList<DebugVariable>&)> done) = 0;
};

} // namespace eudaimonia

#define EUDAIMONIA_DEBUGGER_IID "io.github.eudahub.eudaimonia.Debugger/1"
Q_DECLARE_INTERFACE(eudaimonia::Debugger, EUDAIMONIA_DEBUGGER_IID)

#endif // EUDAIMONIA_PLUGIN_DEBUGGER_H
