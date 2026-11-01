// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>

namespace eudaimonia {

// Project directories opened before, newest first, kept in recent-projects.json
// in the application's local data directory.
class RecentProjects {
public:
    // An empty path means recent-projects.json in AppLocalDataLocation.
    explicit RecentProjects(QString filePath = {});

    const QStringList& paths() const { return m_paths; }
    // Moves the directory to the front and saves the list atomically.
    bool remember(const QString& path, QString* error = nullptr);

private:
    QString m_filePath;
    QStringList m_paths;
};

} // namespace eudaimonia
