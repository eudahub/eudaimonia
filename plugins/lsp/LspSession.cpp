// SPDX-License-Identifier: GPL-3.0-only
#include "LspSession.h"

#include "../common/ContentLengthMessages.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace eudaimonia::lsp {

LspSession::LspSession(const ServerSpec& spec, const QString& projectDir, const QString& buildDir, QObject* parent)
    : QObject(parent), m_spec(spec), m_projectDir(projectDir), m_buildDir(buildDir) {}

LspSession::~LspSession() {
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool LspSession::isRunning() const {
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool LspSession::start() {
    const QString program = QStandardPaths::findExecutable(m_spec.program);
    if (program.isEmpty()) return false;
    QStringList arguments;
    for (QString argument : m_spec.arguments) {
        argument.replace(QStringLiteral("${buildDir}"), m_buildDir);
        argument.replace(QStringLiteral("${projectDir}"), m_projectDir);
        arguments << argument;
    }
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_projectDir);
    // The server's own messages (stderr) are its log, not the protocol.
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setStandardErrorFile(QProcess::nullDevice());
    connect(m_process, &QProcess::readyReadStandardOutput, this, &LspSession::readOutput);
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        if (!m_stopping) emit ended(tr("%1 ended (exit code %2)").arg(m_spec.name).arg(exitCode));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) emit ended(tr("%1 could not start: %2").arg(m_spec.name, m_process->errorString()));
    });
    m_process->start(program, arguments);

    const QString root = QUrl::fromLocalFile(m_projectDir).toString();
    QJsonObject capabilities{
        {QStringLiteral("textDocument"),
         QJsonObject{{QStringLiteral("synchronization"), QJsonObject{{QStringLiteral("didSave"), true}}},
                     {QStringLiteral("publishDiagnostics"), QJsonObject{{QStringLiteral("relatedInformation"), false}}},
                     {QStringLiteral("definition"), QJsonObject{{QStringLiteral("linkSupport"), true}}}}},
        {QStringLiteral("general"), QJsonObject{{QStringLiteral("positionEncodings"), QJsonArray{QStringLiteral("utf-16")}}}}};
    const QJsonObject params{
        {QStringLiteral("processId"), qint64(QCoreApplication::applicationPid())},
        {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Eudaimonia")}}},
        {QStringLiteral("rootUri"), root},
        {QStringLiteral("workspaceFolders"),
         QJsonArray{QJsonObject{{QStringLiteral("uri"), root}, {QStringLiteral("name"), m_projectDir.section('/', -1)}}}},
        {QStringLiteral("capabilities"), capabilities}};
    // "initialize" goes first; everything else waits for its answer.
    const int id = m_nextId++;
    m_pending.insert(id, [this](const QJsonValue&) {
        m_initialized = true;
        send(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                         {QStringLiteral("method"), QStringLiteral("initialized")},
                         {QStringLiteral("params"), QJsonObject{}}});
        const QList<QJsonObject> queued = m_queue;
        m_queue.clear();
        for (const QJsonObject& message : queued) send(message);
    });
    send(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                     {QStringLiteral("id"), id},
                     {QStringLiteral("method"), QStringLiteral("initialize")},
                     {QStringLiteral("params"), params}});
    return true;
}

void LspSession::request(const QString& method, const QJsonObject& params, ResultHandler handler) {
    const int id = m_nextId++;
    m_pending.insert(id, std::move(handler));
    const QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                              {QStringLiteral("id"), id},
                              {QStringLiteral("method"), method},
                              {QStringLiteral("params"), params}};
    if (m_initialized) send(message);
    else m_queue << message;
}

void LspSession::notify(const QString& method, const QJsonObject& params) {
    const QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                              {QStringLiteral("method"), method},
                              {QStringLiteral("params"), params}};
    if (m_initialized) send(message);
    else m_queue << message;
}

void LspSession::stop() {
    if (!isRunning()) return;
    m_stopping = true;
    if (m_initialized) {
        request(QStringLiteral("shutdown"), {}, [this](const QJsonValue&) {
            notify(QStringLiteral("exit"), {});
            if (m_process) m_process->closeWriteChannel();
        });
    }
    // A server that does not end by itself is killed.
    QTimer::singleShot(2000, this, [this] {
        if (isRunning()) m_process->kill();
    });
}

QByteArray LspSession::frame(const QJsonObject& message) {
    return messages::frame(message);
}

void LspSession::send(const QJsonObject& message) {
    if (isRunning() || (m_process && m_process->state() == QProcess::Starting)) m_process->write(frame(message));
}

QList<QJsonObject> LspSession::takeMessages(QByteArray& buffer) {
    return messages::take(buffer);
}

void LspSession::readOutput() {
    m_buffer += m_process->readAllStandardOutput();
    for (const QJsonObject& message : takeMessages(m_buffer)) handle(message);
}

void LspSession::handle(const QJsonObject& message) {
    const bool hasId = message.contains(QStringLiteral("id"));
    const QString method = message.value(QStringLiteral("method")).toString();
    if (hasId && method.isEmpty()) {
        // An answer to a request of ours.
        const int id = message.value(QStringLiteral("id")).toInt();
        const ResultHandler handler = m_pending.take(id);
        if (handler) handler(message.value(QStringLiteral("result")));
        return;
    }
    if (hasId) {
        // A request of the server (workspace/configuration,
        // window/workDoneProgress/create, client/registerCapability, ...):
        // answered with nothing, so that it does not wait.
        QJsonValue result;
        if (method == QLatin1String("workspace/configuration")) {
            QJsonArray items;
            for (int i = 0; i < message.value(QStringLiteral("params")).toObject().value(QStringLiteral("items")).toArray().size(); ++i)
                items << QJsonValue();
            result = items;
        }
        send(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                         {QStringLiteral("id"), message.value(QStringLiteral("id"))},
                         {QStringLiteral("result"), result}});
        return;
    }
    emit notification(method, message.value(QStringLiteral("params")).toObject());
}

} // namespace eudaimonia::lsp
