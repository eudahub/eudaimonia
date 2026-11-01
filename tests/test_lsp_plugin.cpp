// SPDX-License-Identifier: GPL-3.0-only
// The language support plugin, loaded from its file, with a language server
// written for the test (tests/lsp/fake_server.py, needs python3) and one that
// is not installed; and the framing of the protocol's messages.
#include "LspSession.h"

#include <eudaimonia/plugin/LanguageSupport.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPluginLoader>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace eudaimonia;

namespace {

struct Listener : LanguageSupportListener {
    QHash<QString, QList<Diagnostic>> diagnosticsOf;
    int reports = 0;
    QStringList messages;
    void diagnostics(const QString& file, const QList<Diagnostic>& list) override {
        diagnosticsOf[file] = list;
        ++reports;
    }
    void message(const QString& text) override { messages << text; }
};

} // namespace

class TestLspPlugin : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_project;
    QString m_log;
    LanguageSupport* m_lsp = nullptr;
    Listener m_listener;

    QString file(const QString& name, const QByteArray& text) {
        const QString path = m_project.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile out(path);
        if (out.open(QIODevice::WriteOnly)) out.write(text);
        return path;
    }

    QStringList logged() {
        QFile in(m_log);
        return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()).split('\n', Qt::SkipEmptyParts)
                                            : QStringList();
    }

private slots:
    void initTestCase() {
        if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) QSKIP("python3 is not installed");
        QVERIFY(m_project.isValid());
        m_log = m_project.filePath(QStringLiteral("server.log"));
        // The test's servers, through the file the plugin reads at start.
        const QJsonArray servers{
            QJsonObject{{"name", "fake"}, {"languages", "Fake"}, {"program", "python3"},
                        {"arguments", QJsonArray{QStringLiteral(FAKE_SERVER), m_log}},
                        {"suffixes", QJsonArray{"fake"}}, {"languageId", "fake"}},
            QJsonObject{{"name", "missing"}, {"languages", "Missing"}, {"program", "no-such-language-server"},
                        {"suffixes", QJsonArray{"nope"}}, {"installHint", "apt install missing"}}};
        const QString config = file(QStringLiteral("servers.json"), QJsonDocument(servers).toJson());
        qputenv("EUDAIMONIA_LSP_SERVERS", config.toUtf8());
        static QPluginLoader loader(QStringLiteral(LSP_PLUGIN_FILE));
        m_lsp = qobject_cast<LanguageSupport*>(loader.instance());
        QVERIFY2(m_lsp, qPrintable(loader.errorString()));
        m_lsp->setListener(&m_listener);
    }

    void framesMessages() {
        const QJsonObject first{{"jsonrpc", "2.0"}, {"method", "a"}};
        const QJsonObject second{{"jsonrpc", "2.0"}, {"id", 7}, {"result", "ż"}};
        const QByteArray stream = lsp::LspSession::frame(first) + lsp::LspSession::frame(second);
        // Bytes coming in pieces: whole messages only, in order.
        QByteArray buffer = stream.left(10);
        QVERIFY(lsp::LspSession::takeMessages(buffer).isEmpty());
        buffer += stream.mid(10, stream.size() - 12);
        QCOMPARE(lsp::LspSession::takeMessages(buffer).size(), 1);
        buffer += stream.right(2);
        const QList<QJsonObject> rest = lsp::LspSession::takeMessages(buffer);
        QCOMPARE(rest.size(), 1);
        QCOMPARE(rest.first().value("result").toString(), QStringLiteral("ż"));
        QVERIFY(buffer.isEmpty());
    }

    void findsTheProjectsServers() {
        // A file only in a build directory does not count.
        QTemporaryDir onlyBuilt;
        QDir(onlyBuilt.path()).mkpath(QStringLiteral("build"));
        QFile built(onlyBuilt.filePath(QStringLiteral("build/x.nope")));
        QVERIFY(built.open(QIODevice::WriteOnly));
        built.close();
        QVERIFY(m_lsp->serversFor(onlyBuilt.path()).isEmpty());

        file(QStringLiteral("src/a.fake"), "x");
        file(QStringLiteral("docs/b.nope"), "x");
        const QList<LanguageServerInfo> servers = m_lsp->serversFor(m_project.path());
        QCOMPARE(servers.size(), 2);
        for (const LanguageServerInfo& server : servers) {
            if (server.name == QLatin1String("fake")) {
                QVERIFY(server.installed);
            } else {
                QCOMPARE(server.name, QStringLiteral("missing"));
                QVERIFY(!server.installed);
                QCOMPARE(server.installHint, QStringLiteral("apt install missing"));
            }
        }
        QVERIFY(m_lsp->handles(m_project.filePath("src/a.fake")));
        QVERIFY(!m_lsp->handles(m_project.filePath("docs/b.nope")));
        QVERIFY(!m_lsp->handles(m_project.filePath("x.txt")));
    }

    void reportsDiagnosticsAsTheTextChanges() {
        m_lsp->setProject(m_project.path(), m_project.filePath("build"));
        const QString path = m_project.filePath("src/a.fake");
        m_lsp->opened(path, QStringLiteral("good\nthis is bad\n"));
        QTRY_VERIFY(m_listener.diagnosticsOf.contains(path));
        QCOMPARE(m_listener.diagnosticsOf.value(path).size(), 1);
        const Diagnostic diagnostic = m_listener.diagnosticsOf.value(path).first();
        QCOMPARE(diagnostic.start.line, 1);
        QCOMPARE(diagnostic.start.column, 8);
        QCOMPARE(diagnostic.end.column, 11);
        QCOMPARE(diagnostic.severity, Diagnostic::Severity::Error);
        QCOMPARE(diagnostic.message, QStringLiteral("bad word"));

        // Changes in a row are sent once, after a pause.
        const int before = m_listener.reports;
        m_lsp->changed(path, QStringLiteral("good\n"));
        m_lsp->changed(path, QStringLiteral("good\nfine\n"));
        QTRY_VERIFY(m_listener.reports > before);
        QVERIFY(m_listener.diagnosticsOf.value(path).isEmpty());
        QCOMPARE(logged().count(QStringLiteral("textDocument/didChange")), 1);
        // The server's own request was answered.
        QVERIFY(logged().contains(QStringLiteral("response server-1")));
    }

    void findsADefinition() {
        const QString path = m_project.filePath("src/a.fake");
        QList<SourceLocation> found;
        bool answered = false;
        m_lsp->findDefinition(path, {1, 2}, [&](const QList<SourceLocation>& locations) {
            found = locations;
            answered = true;
        });
        QTRY_VERIFY(answered);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().file, path);
        QCOMPARE(found.first().position.line, 0);
        QCOMPARE(found.first().position.column, 4);
        // No server for the file: an answer of nothing, at once.
        bool none = false;
        m_lsp->findDefinition(m_project.filePath("x.txt"), {0, 0}, [&](const QList<SourceLocation>& l) { none = l.isEmpty(); });
        QVERIFY(none);
    }

    void savesClosesAndShutsDown() {
        const QString path = m_project.filePath("src/a.fake");
        m_lsp->changed(path, QStringLiteral("bad again\n"));
        m_lsp->saved(path);  // the pending change goes first
        QTRY_VERIFY(logged().contains(QStringLiteral("textDocument/didSave")));
        const QStringList log = logged();
        QVERIFY(log.lastIndexOf(QStringLiteral("textDocument/didChange")) < log.lastIndexOf(QStringLiteral("textDocument/didSave")));
        m_lsp->closed(path);
        QVERIFY(m_listener.diagnosticsOf.value(path).isEmpty());
        QTRY_VERIFY(logged().contains(QStringLiteral("textDocument/didClose")));
        m_lsp->shutdown();
        QTRY_VERIFY(logged().contains(QStringLiteral("exit")));
        QVERIFY(m_listener.messages.isEmpty());  // a server asked to stop is no news
    }
};

QTEST_GUILESS_MAIN(TestLspPlugin)
#include "test_lsp_plugin.moc"
