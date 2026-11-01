// SPDX-License-Identifier: GPL-3.0-only
// Language support through language servers (the Language Server Protocol):
// one client for any number of servers, chosen by the documents' file names.

#include "LspSession.h"
#include "ServerSpec.h"

#include <eudaimonia/plugin/LanguageSupport.h>

#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace eudaimonia {
namespace {

// Directories of builds, version control and fetched dependencies: their
// files tell nothing of the project's languages.
bool skippedDirectory(const QString& name) {
    return name.startsWith(QLatin1Char('.')) || name.startsWith(QLatin1String("build"))
           || name.startsWith(QLatin1String("cmake-build-")) || name == QLatin1String("_deps")
           || name == QLatin1String("node_modules") || name == QLatin1String("target");
}

QString uriOf(const QString& file) {
    return QUrl::fromLocalFile(file).toString();
}

QJsonObject positionJson(SourcePosition position) {
    return {{QStringLiteral("line"), position.line}, {QStringLiteral("character"), position.column}};
}

SourcePosition positionOf(const QJsonValue& value) {
    const QJsonObject object = value.toObject();
    return {object.value(QStringLiteral("line")).toInt(), object.value(QStringLiteral("character")).toInt()};
}

} // namespace

class LspPlugin : public QObject, public LanguageSupport {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EUDAIMONIA_LANGUAGESUPPORT_IID)
    Q_INTERFACES(eudaimonia::LanguageSupport)

public:
    LspPlugin() : m_servers(lsp::knownServers()) {}
    ~LspPlugin() override { shutdown(); }

    QList<LanguageServerInfo> serversFor(const QString& projectDir) const override {
        const QList<lsp::ServerSpec> servers = lsp::knownServers();
        QSet<QString> needed;
        // A walk over the project's own files, at most this many.
        int budget = 50000;
        QList<QString> directories{projectDir};
        while (!directories.isEmpty() && budget > 0 && needed.size() < servers.size()) {
            const QDir directory(directories.takeLast());
            for (const QFileInfo& entry :
                 directory.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
                if (--budget <= 0) break;
                if (entry.isDir()) {
                    if (!entry.isSymLink() && !skippedDirectory(entry.fileName())) directories << entry.filePath();
                    continue;
                }
                for (const lsp::ServerSpec& spec : servers)
                    if (spec.matches(entry.fileName())) needed.insert(spec.name);
            }
        }
        QList<LanguageServerInfo> result;
        for (const lsp::ServerSpec& spec : servers) {
            if (!needed.contains(spec.name)) continue;
            result << LanguageServerInfo{spec.name, spec.languages,
                                         !QStandardPaths::findExecutable(spec.program).isEmpty(), spec.installHint};
        }
        return result;
    }

    void setProject(const QString& projectDir, const QString& buildDir) override {
        if (projectDir == m_projectDir && buildDir == m_buildDir) return;
        shutdown();
        m_projectDir = projectDir;
        m_buildDir = buildDir;
        // A changed language-servers.json counts from the next project on.
        m_servers = lsp::knownServers();
    }

    void setListener(LanguageSupportListener* listener) override { m_listener = listener; }

    bool handles(const QString& file) const override {
        const lsp::ServerSpec* spec = specFor(file);
        return spec && !QStandardPaths::findExecutable(spec->program).isEmpty();
    }

    void opened(const QString& file, const QString& text) override {
        if (m_documents.contains(file)) {
            changed(file, text);
            return;
        }
        const lsp::ServerSpec* spec = specFor(file);
        if (!spec) return;
        lsp::LspSession* session = sessionFor(*spec);
        if (!session) return;
        Document& document = m_documents[file];
        document.server = spec->name;
        document.version = 1;
        session->notify(QStringLiteral("textDocument/didOpen"),
                        {{QStringLiteral("textDocument"),
                          QJsonObject{{QStringLiteral("uri"), uriOf(file)},
                                      {QStringLiteral("languageId"), spec->languageIdOf(file)},
                                      {QStringLiteral("version"), 1},
                                      {QStringLiteral("text"), text}}}});
    }

    void changed(const QString& file, const QString& text) override {
        auto document = m_documents.find(file);
        if (document == m_documents.end()) return;
        // Typing sends the text once it rests for a moment.
        document->pendingText = text;
        document->hasPending = true;
        if (!document->timer) {
            document->timer = new QTimer(this);
            document->timer->setSingleShot(true);
            document->timer->setInterval(300);
            connect(document->timer, &QTimer::timeout, this, [this, file] { flush(file); });
        }
        document->timer->start();
    }

    void saved(const QString& file) override {
        if (!m_documents.contains(file)) return;
        flush(file);
        if (lsp::LspSession* session = m_sessions.value(m_documents.value(file).server))
            session->notify(QStringLiteral("textDocument/didSave"),
                            {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uriOf(file)}}}});
    }

    void closed(const QString& file) override {
        auto document = m_documents.find(file);
        if (document == m_documents.end()) return;
        if (document->timer) document->timer->deleteLater();
        const QString server = document->server;
        m_documents.erase(document);
        if (lsp::LspSession* session = m_sessions.value(server))
            session->notify(QStringLiteral("textDocument/didClose"),
                            {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uriOf(file)}}}});
        if (m_listener) m_listener->diagnostics(file, {});
    }

    void findDefinition(const QString& file, SourcePosition position,
                        std::function<void(const QList<SourceLocation>&)> done) override {
        lsp::LspSession* session = m_documents.contains(file) ? m_sessions.value(m_documents.value(file).server) : nullptr;
        if (!session) {
            done({});
            return;
        }
        flush(file);  // the server must know the text the position is in
        session->request(QStringLiteral("textDocument/definition"),
                         {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uriOf(file)}}},
                          {QStringLiteral("position"), positionJson(position)}},
                         [done](const QJsonValue& result) {
                             // Location, Location[] or LocationLink[].
                             const QJsonArray items = result.isArray() ? result.toArray() : QJsonArray{result};
                             QList<SourceLocation> locations;
                             for (const QJsonValue& item : items) {
                                 const QJsonObject object = item.toObject();
                                 const bool link = object.contains(QStringLiteral("targetUri"));
                                 const QString uri = object.value(link ? QStringLiteral("targetUri") : QStringLiteral("uri")).toString();
                                 const QJsonObject range =
                                     object.value(link ? QStringLiteral("targetSelectionRange") : QStringLiteral("range")).toObject();
                                 if (uri.isEmpty()) continue;
                                 locations << SourceLocation{QUrl(uri).toLocalFile(), positionOf(range.value(QStringLiteral("start")))};
                             }
                             done(locations);
                         });
    }

    void shutdown() override {
        for (Document& document : m_documents)
            if (document.timer) document.timer->deleteLater();
        m_documents.clear();
        for (lsp::LspSession* session : std::as_const(m_sessions)) {
            disconnect(session, nullptr, this, nullptr);
            session->stop();
            // Gone once it ended, or after its time to end.
            QTimer::singleShot(3000, session, &QObject::deleteLater);
        }
        m_sessions.clear();
    }

private:
    struct Document {
        QString server;
        int version = 1;
        QString pendingText;
        bool hasPending = false;
        QPointer<QTimer> timer;
    };

    const lsp::ServerSpec* specFor(const QString& file) const {
        for (const lsp::ServerSpec& spec : m_servers)
            if (spec.matches(file)) return &spec;
        return nullptr;
    }

    lsp::LspSession* sessionFor(const lsp::ServerSpec& spec) {
        if (lsp::LspSession* session = m_sessions.value(spec.name)) return session;
        if (m_failed.contains(spec.name)) return nullptr;
        auto* session = new lsp::LspSession(spec, m_projectDir, m_buildDir, this);
        if (!session->start()) {
            m_failed.insert(spec.name);
            delete session;
            return nullptr;
        }
        connect(session, &lsp::LspSession::notification, this,
                [this](const QString& method, const QJsonObject& params) { notification(method, params); });
        connect(session, &lsp::LspSession::ended, this, [this, session](const QString& why) {
            // Its documents are no longer known to any server.
            const QString name = session->spec().name;
            for (auto it = m_documents.begin(); it != m_documents.end();) {
                if (it->server == name) {
                    if (m_listener) m_listener->diagnostics(it.key(), {});
                    it = m_documents.erase(it);
                } else {
                    ++it;
                }
            }
            m_sessions.remove(name);
            m_failed.insert(name);
            session->deleteLater();
            if (m_listener) m_listener->message(why);
        });
        m_sessions.insert(spec.name, session);
        return session;
    }

    void flush(const QString& file) {
        auto document = m_documents.find(file);
        if (document == m_documents.end() || !document->hasPending) return;
        if (document->timer) document->timer->stop();
        document->hasPending = false;
        ++document->version;
        if (lsp::LspSession* session = m_sessions.value(document->server))
            session->notify(QStringLiteral("textDocument/didChange"),
                            {{QStringLiteral("textDocument"),
                              QJsonObject{{QStringLiteral("uri"), uriOf(file)}, {QStringLiteral("version"), document->version}}},
                             {QStringLiteral("contentChanges"),
                              QJsonArray{QJsonObject{{QStringLiteral("text"), document->pendingText}}}}});
    }

    void notification(const QString& method, const QJsonObject& params) {
        if (!m_listener) return;
        if (method == QLatin1String("textDocument/publishDiagnostics")) {
            const QString file = QUrl(params.value(QStringLiteral("uri")).toString()).toLocalFile();
            QList<Diagnostic> list;
            for (const QJsonValue& value : params.value(QStringLiteral("diagnostics")).toArray()) {
                const QJsonObject object = value.toObject();
                const QJsonObject range = object.value(QStringLiteral("range")).toObject();
                Diagnostic diagnostic;
                diagnostic.start = positionOf(range.value(QStringLiteral("start")));
                diagnostic.end = positionOf(range.value(QStringLiteral("end")));
                diagnostic.severity = Diagnostic::Severity(object.value(QStringLiteral("severity")).toInt(1));
                diagnostic.message = object.value(QStringLiteral("message")).toString();
                diagnostic.source = object.value(QStringLiteral("source")).toString();
                list << diagnostic;
            }
            m_listener->diagnostics(file, list);
        } else if (method == QLatin1String("window/showMessage")) {
            m_listener->message(params.value(QStringLiteral("message")).toString());
        }
    }

    QList<lsp::ServerSpec> m_servers;
    QString m_projectDir;
    QString m_buildDir;
    LanguageSupportListener* m_listener = nullptr;
    QHash<QString, lsp::LspSession*> m_sessions;  // by server name
    QSet<QString> m_failed;                       // servers that cannot run in this project
    QHash<QString, Document> m_documents;         // by file
};

} // namespace eudaimonia

#include "LspPlugin.moc"
