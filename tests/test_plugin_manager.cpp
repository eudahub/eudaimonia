// SPDX-License-Identifier: GPL-3.0-only
#include "PluginManager.h"
#include "plugin/TestInterface.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QTest>

using eudaimonia::PluginManager;

// The test plugins are built into TEST_PLUGIN_DIR/first and TEST_PLUGIN_DIR/second.
class TestPluginManager : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_notPlugins;
    QTemporaryDir m_copies;
    const QString m_first = QStringLiteral(TEST_PLUGIN_DIR "/first");
    const QString m_second = QStringLiteral(TEST_PLUGIN_DIR "/second");

    static QString pluginFile(const QString& directory) {
        const QStringList files = QDir(directory).entryList(QDir::Files);
        return files.isEmpty() ? QString() : QDir(directory).absoluteFilePath(files.first());
    }

    static bool reportHas(const PluginManager& plugins, const QString& text) {
        for (const QString& line : plugins.report())
            if (line.contains(text)) return true;
        return false;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_notPlugins.isValid() && m_copies.isValid());
        QFile fake(m_notPlugins.filePath("libfake.so"));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("not an ELF object");
        fake.close();
        qputenv("EUDAIMONIA_PLUGIN_PATH", QStringList({m_first, m_second, m_notPlugins.path()}).join(':').toUtf8());
    }

    void searchesThePathFirst() {
        const QStringList directories = PluginManager::directories();
        QVERIFY(directories.size() >= 3);
        QCOMPARE(directories.mid(0, 3), QStringList({m_first, m_second, m_notPlugins.path()}));
    }

    void reportsWhatWasFound() {
        const PluginManager plugins;
        const QString first = pluginFile(m_first);
        const QString second = pluginFile(m_second);
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QVERIFY(reportHas(plugins, first + ": " TestInterface_iid));
        QVERIFY(reportHas(plugins, second + ": " TestInterface_iid));
        QVERIFY(reportHas(plugins, m_notPlugins.filePath("libfake.so") + ": not a Qt plugin"));
    }

    void loadsOnFirstUseTheFirstFound() {
        PluginManager plugins;
        const QString first = pluginFile(m_first);
        QVERIFY(!QPluginLoader(first).isLoaded());
        auto* plugin = plugins.plugin<TestInterface>();
        QVERIFY(plugin);
        QCOMPARE(plugin->name(), QStringLiteral("first"));
        QVERIFY(QPluginLoader(first).isLoaded());
        QVERIFY(!QPluginLoader(pluginFile(m_second)).isLoaded());
    }

    // Several files may provide an interface; a file named as an earlier one
    // (an installed plugin and the build tree's) is left out.
    void givesEveryPluginOfAnInterfaceOnce() {
        const QString first = pluginFile(m_first);
        const QString copy = m_copies.filePath(QFileInfo(first).fileName());
        QVERIFY(QFile::copy(first, copy));
        const QByteArray path = qgetenv("EUDAIMONIA_PLUGIN_PATH");
        qputenv("EUDAIMONIA_PLUGIN_PATH", path + ':' + m_copies.path().toUtf8());
        PluginManager plugins;
        QVERIFY(reportHas(plugins, copy + ": " TestInterface_iid ", not used: " + first + " goes first"));
        const QList<TestInterface*> all = plugins.plugins<TestInterface>();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all.at(0)->name(), QStringLiteral("first"));
        QCOMPARE(all.at(1)->name(), QStringLiteral("second"));
        qputenv("EUDAIMONIA_PLUGIN_PATH", path);
    }

    void missingInterface() {
        PluginManager plugins;
        const QString iid = QStringLiteral("io.github.eudahub.eudaimonia.Missing/1");
        QVERIFY(!plugins.instance(iid));
        const QString message = plugins.missingMessage(iid, QStringLiteral("Editing"));
        QVERIFY(message.contains(iid));
        QVERIFY(message.contains(m_second));
    }
};

QTEST_GUILESS_MAIN(TestPluginManager)
#include "test_plugin_manager.moc"
