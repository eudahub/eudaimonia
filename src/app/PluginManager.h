// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

class QPluginLoader;

namespace eudaimonia {

// Finds the Qt plugins in the plugin directories: $EUDAIMONIA_PLUGIN_PATH
// (directories separated by ':'), the installed lib/eudaimonia/plugins next to
// bin, and plugins/ next to the executable in the build tree. The plugin files
// are only read for their interface ID (IID) at start; a plugin is loaded when
// its interface is first asked for. A file named as one found before is left
// out (an installed plugin and the build tree's). An interface may have several
// plugins, e.g. one build system each: plugin() gives the first, plugins() all.
//
// A missing plugin is an error for the feature that needs it: there are no
// fallbacks. missingMessage() tells the user what was searched.
class PluginManager {
public:
    PluginManager();
    ~PluginManager();
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // The directories searched, in order.
    static QStringList directories();

    // The plugin implementing the interface, loaded on first use; null when
    // there is none or it cannot be loaded (see report()).
    QObject* instance(const QString& iid);
    template <typename Interface>
    Interface* plugin() {
        return qobject_cast<Interface*>(instance(QString::fromLatin1(qobject_interface_iid<Interface*>())));
    }
    // Every plugin implementing the interface that can be loaded, by file name.
    QList<QObject*> instances(const QString& iid);
    template <typename Interface>
    QList<Interface*> plugins() {
        QList<Interface*> result;
        for (QObject* object : instances(QString::fromLatin1(qobject_interface_iid<Interface*>())))
            if (auto* plugin = qobject_cast<Interface*>(object)) result << plugin;
        return result;
    }

    // One line per plugin file or problem, for the Plugins dialog and logs.
    const QStringList& report() const { return m_report; }
    // Why the plugin for @p iid is missing, with the searched directories.
    QString missingMessage(const QString& iid, const QString& feature) const;

private:
    QHash<QString, QList<QPluginLoader*>> m_loaders;  // by IID
    QStringList m_report;
};

} // namespace eudaimonia
