// SPDX-License-Identifier: GPL-3.0-only
#include "DapSession.h"

#include "../common/ContentLengthMessages.h"

#include <QDir>
#include <QJsonValue>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

namespace eudaimonia::dap {
namespace {

// How long an adapter gets to open its socket: 50 ms each, 15 s in all.
constexpr int maxConnectAttempts = 300;

} // namespace

DapSession::DapSession(QObject* parent) : QObject(parent) {}

DapSession::~DapSession() {
    if (m_socket) disconnect(m_socket, nullptr, this, nullptr);
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void DapSession::watchProcess(bool stdio) {
    connect(m_process, &QProcess::finished, this, [this] {
        if (m_stopping) finish({});
        else if (m_socket && m_socket->state() == QLocalSocket::ConnectedState) return;  // the socket tells
        else finish(tr("%1 ended unexpectedly.\n%2")
                        .arg(m_process->program(), QString::fromLocal8Bit(m_adapterOutput).trimmed()));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(tr("%1 could not start: %2").arg(m_process->program(), m_process->errorString()));
    });
    if (stdio) {
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] { received(m_process->readAllStandardOutput()); });
    } else {
        // The adapter's own output is what the debugged script writes.
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            const QByteArray bytes = m_process->readAllStandardOutput();
            m_adapterOutput += bytes;
            emit output(QString::fromLocal8Bit(bytes));
        });
    }
}

void DapSession::startOverStdio(const QString& program, const QStringList& arguments, const QString& workingDirectory) {
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(workingDirectory);
    m_process->setStandardErrorFile(QProcess::nullDevice());
    watchProcess(true);
    connect(m_process, &QProcess::started, this, &DapSession::connected);
    m_process->start(program, arguments);
}

void DapSession::startOverSocket(const QString& program, QStringList arguments, const QString& workingDirectory,
                                 const QStringList& socketArguments) {
    // A short path: socket names are limited in length.
    m_socketDirectory = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/eudaimonia-dap-XXXXXX"));
    m_socketPath = m_socketDirectory->filePath(QStringLiteral("s"));
    arguments = socketArguments + QStringList{m_socketPath} + arguments;
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(workingDirectory);
    watchProcess(false);
    m_process->start(program, arguments);

    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::connected, this, [this] {
        m_connecting->stop();
        emit connected();
    });
    connect(m_socket, &QLocalSocket::readyRead, this, [this] { received(m_socket->readAll()); });
    connect(m_socket, &QLocalSocket::disconnected, this, [this] {
        // The script ended: the adapter finishes by itself, its end ends this.
        if (m_process->state() != QProcess::NotRunning) m_stopping = true;
        else finish({});
    });
    // The adapter opens the socket a moment after it starts.
    m_connecting = new QTimer(this);
    m_connecting->setInterval(50);
    connect(m_connecting, &QTimer::timeout, this, &DapSession::connectSocket);
    m_connecting->start();
}

void DapSession::connectSocket() {
    if (m_socket->state() != QLocalSocket::UnconnectedState) return;
    if (++m_connectAttempts > maxConnectAttempts) {
        m_connecting->stop();
        m_process->kill();
        finish(tr("%1 did not open its debugger socket.").arg(m_process->program()));
        return;
    }
    if (QFile::exists(m_socketPath)) m_socket->connectToServer(m_socketPath);
}

void DapSession::request(const QString& command, const QJsonObject& arguments, Response done) {
    const int seq = m_nextSeq++;
    if (done) m_pending.insert(seq, std::move(done));
    send({{QStringLiteral("seq"), seq},
          {QStringLiteral("type"), QStringLiteral("request")},
          {QStringLiteral("command"), command},
          {QStringLiteral("arguments"), arguments}});
}

void DapSession::send(const QJsonObject& message) {
    const QByteArray bytes = messages::frame(message);
    if (m_socket) {
        if (m_socket->state() == QLocalSocket::ConnectedState) m_socket->write(bytes);
    } else if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->write(bytes);
    }
}

void DapSession::received(const QByteArray& bytes) {
    m_buffer += bytes;
    for (const QJsonObject& message : messages::take(m_buffer)) handle(message);
}

void DapSession::handle(const QJsonObject& message) {
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("response")) {
        const Response done = m_pending.take(message.value(QStringLiteral("request_seq")).toInt());
        if (done)
            done(message.value(QStringLiteral("success")).toBool(), message.value(QStringLiteral("body")).toObject(),
                 message.value(QStringLiteral("message")).toString());
    } else if (type == QLatin1String("event")) {
        emit event(message.value(QStringLiteral("event")).toString(), message.value(QStringLiteral("body")).toObject());
    }
}

void DapSession::stop(const QJsonObject& disconnectArguments) {
    if (m_stopping || m_finished) return;
    m_stopping = true;
    if (m_connecting) m_connecting->stop();
    request(QStringLiteral("disconnect"), disconnectArguments);
    // What does not end by itself is asked, then made to.
    QTimer::singleShot(1000, this, [this] {
        if (m_process && m_process->state() != QProcess::NotRunning) m_process->terminate();
    });
    QTimer::singleShot(3000, this, [this] {
        if (m_process && m_process->state() != QProcess::NotRunning) m_process->kill();
    });
}

void DapSession::debuggeeEnded() {
    if (m_stopping || m_finished) return;
    m_stopping = true;
    request(QStringLiteral("disconnect"));
    // An adapter over stdio has nothing left to do; CMake may still be
    // generating the build system, and is left to finish.
    if (!m_socket) {
        QTimer::singleShot(3000, this, [this] {
            if (m_process && m_process->state() != QProcess::NotRunning) m_process->kill();
        });
    }
}

void DapSession::finish(const QString& error) {
    if (m_finished) return;
    m_finished = true;
    if (m_connecting) m_connecting->stop();
    emit ended(error);
}

} // namespace eudaimonia::dap
