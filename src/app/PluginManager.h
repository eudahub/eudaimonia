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
// its interface is first asked for. For each IID the first plugin found is used.
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

    // One line per plugin file or problem, for the Plugins dialog and logs.
    const QStringList& report() const { return m_report; }
    // Why the plugin for @p iid is missing, with the searched directories.
    QString missingMessage(const QString& iid, const QString& feature) const;

private:
    QHash<QString, QPluginLoader*> m_loaders;  // by IID
    QStringList m_report;
};

} // namespace eudaimonia
