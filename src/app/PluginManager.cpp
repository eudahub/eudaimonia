// SPDX-License-Identifier: GPL-3.0-only
#include "PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>

namespace eudaimonia {

QStringList PluginManager::directories() {
    QStringList result;
    const QString variable = qEnvironmentVariable("EUDAIMONIA_PLUGIN_PATH");
    for (const QString& dir : variable.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        result << QDir::cleanPath(QDir(dir).absolutePath());
    const QString bin = QCoreApplication::applicationDirPath();
    // Set by CMake, e.g. "../lib/eudaimonia/plugins".
    result << QDir::cleanPath(bin + QStringLiteral("/" EUDAIMONIA_PLUGIN_DIR_FROM_BIN));
    result << QDir::cleanPath(bin + QStringLiteral("/plugins"));  // build tree
    result.removeDuplicates();
    return result;
}

PluginManager::PluginManager() {
    QHash<QString, QString> found;  // the files so far, by name
    for (const QString& path : directories()) {
        const QDir dir(path);
        if (!dir.exists()) continue;
        for (const QString& name : dir.entryList(QDir::Files, QDir::Name)) {
            const QString file = dir.absoluteFilePath(name);
            if (!QLibrary::isLibrary(file)) continue;
            auto* loader = new QPluginLoader(file);
            // Reads the metadata without loading the library.
            const QString iid = loader->metaData().value(QStringLiteral("IID")).toString();
            if (iid.isEmpty()) {
                m_report << QStringLiteral("%1: not a Qt plugin: %2").arg(file, loader->errorString());
                delete loader;
            } else if (found.contains(name)) {
                m_report << QStringLiteral("%1: %2, not used: %3 goes first").arg(file, iid, found.value(name));
                delete loader;
            } else {
                m_report << QStringLiteral("%1: %2").arg(file, iid);
                found.insert(name, file);
                m_loaders[iid] << loader;
            }
        }
    }
}

PluginManager::~PluginManager() {
    // The plugins stay loaded until the program ends: widgets they made may
    // still exist while the window is destroyed.
    for (const QList<QPluginLoader*>& loaders : std::as_const(m_loaders)) qDeleteAll(loaders);
}

QObject* PluginManager::instance(const QString& iid) {
    QList<QPluginLoader*>& loaders = m_loaders[iid];
    while (!loaders.isEmpty()) {
        QPluginLoader* loader = loaders.first();
        if (QObject* plugin = loader->instance()) return plugin;
        m_report << QStringLiteral("%1: %2 not loaded: %3").arg(loader->fileName(), iid, loader->errorString());
        delete loader;
        loaders.removeFirst();
    }
    return nullptr;
}

QList<QObject*> PluginManager::instances(const QString& iid) {
    QList<QObject*> result;
    QList<QPluginLoader*>& loaders = m_loaders[iid];
    for (auto loader = loaders.begin(); loader != loaders.end();) {
        if (QObject* plugin = (*loader)->instance()) {
            result << plugin;
            ++loader;
        } else {
            m_report << QStringLiteral("%1: %2 not loaded: %3").arg((*loader)->fileName(), iid, (*loader)->errorString());
            delete *loader;
            loader = loaders.erase(loader);
        }
    }
    return result;
}

QString PluginManager::missingMessage(const QString& iid, const QString& feature) const {
    return QCoreApplication::translate("PluginManager",
                                       "%1 needs a plugin with the interface %2, which was not found "
                                       "or could not be loaded.\n\nSearched directories:\n%3\n\n"
                                       "Directories can be added in EUDAIMONIA_PLUGIN_PATH "
                                       "(separated by ':'). Help → Plugins lists what was found.")
        .arg(feature, iid, directories().join(QLatin1Char('\n')));
}

} // namespace eudaimonia
