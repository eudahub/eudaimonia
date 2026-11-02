// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QLocalSocket;
class QProcess;
class QTemporaryDir;
class QTimer;

namespace eudaimonia::dap {

// One debug adapter speaking the Debug Adapter Protocol: its requests,
// responses and events, framed by "Content-Length", over the adapter's standard
// input and output (gdb -i dap) or over a local socket it listens on (cmake
// --debugger).
class DapSession : public QObject {
    Q_OBJECT
public:
    explicit DapSession(QObject* parent = nullptr);
    ~DapSession() override;

    // Starts the adapter, talking over its standard input and output.
    void startOverStdio(const QString& program, const QStringList& arguments, const QString& workingDirectory);
    // Starts the adapter with a socket name appended by `socketArguments`,
    // and connects to it; what the adapter writes comes through output().
    void startOverSocket(const QString& program, QStringList arguments, const QString& workingDirectory,
                         const QStringList& socketArguments);

    // `done` gets whether it succeeded, the body, and the message of a failure.
    using Response = std::function<void(bool success, const QJsonObject& body, const QString& message)>;
    void request(const QString& command, const QJsonObject& arguments = {}, Response done = {});
    // Ends the adapter: asked first, killed if it lingers.
    void stop(const QJsonObject& disconnectArguments);
    // The debuggee ended by itself: the adapter is told, and ends in its time.
    void debuggeeEnded();

signals:
    // Talking is possible: send "initialize".
    void connected();
    void event(const QString& name, const QJsonObject& body);
    void output(const QString& text);
    // The adapter ended, with why if it could not start or stopped unasked.
    void ended(const QString& error);

private:
    void send(const QJsonObject& message);
    void received(const QByteArray& bytes);
    void handle(const QJsonObject& message);
    void watchProcess(bool stdio);
    void connectSocket();
    void finish(const QString& error);

    QProcess* m_process = nullptr;
    QLocalSocket* m_socket = nullptr;
    std::unique_ptr<QTemporaryDir> m_socketDirectory;
    QString m_socketPath;
    QTimer* m_connecting = nullptr;
    int m_connectAttempts = 0;
    QByteArray m_buffer;
    QByteArray m_adapterOutput;  // for telling why it could not start
    int m_nextSeq = 1;
    QHash<int, Response> m_pending;
    bool m_stopping = false;
    bool m_finished = false;
};

} // namespace eudaimonia::dap
