// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ServerSpec.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class QProcess;

namespace eudaimonia::lsp {

// One running language server: JSON-RPC messages framed by "Content-Length"
// headers over its standard input and output. Messages sent before the
// server answered "initialize" wait until it did.
class LspSession : public QObject {
    Q_OBJECT
public:
    LspSession(const ServerSpec& spec, const QString& projectDir, const QString& buildDir, QObject* parent = nullptr);
    ~LspSession() override;

    const ServerSpec& spec() const { return m_spec; }
    // Starts the server and asks it to initialize; false when it cannot run.
    bool start();
    bool isRunning() const;

    using ResultHandler = std::function<void(const QJsonValue& result)>;
    void request(const QString& method, const QJsonObject& params, ResultHandler handler);
    void notify(const QString& method, const QJsonObject& params);
    // Asks the server to shut down and exit, then kills it if it lingers.
    void stop();

    // Splits incoming bytes into messages; public for tests.
    static QList<QJsonObject> takeMessages(QByteArray& buffer);
    static QByteArray frame(const QJsonObject& message);

signals:
    // A notification from the server, e.g. textDocument/publishDiagnostics.
    void notification(const QString& method, const QJsonObject& params);
    // The server ended or could not start.
    void ended(const QString& why);

private:
    void send(const QJsonObject& message);
    void readOutput();
    void handle(const QJsonObject& message);

    ServerSpec m_spec;
    QString m_projectDir;
    QString m_buildDir;
    QProcess* m_process = nullptr;
    QByteArray m_buffer;
    int m_nextId = 1;
    QHash<int, ResultHandler> m_pending;
    bool m_initialized = false;
    bool m_stopping = false;
    QList<QJsonObject> m_queue;  // to send once initialized
};

} // namespace eudaimonia::lsp
