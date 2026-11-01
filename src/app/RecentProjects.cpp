// SPDX-License-Identifier: GPL-3.0-only
#include "RecentProjects.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace eudaimonia {
namespace {

constexpr int maxRecentProjects = 20;

QString normalized(const QString& path) {
    if (path.isEmpty()) return {};
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

} // namespace

RecentProjects::RecentProjects(QString filePath) : m_filePath(std::move(filePath)) {
    if (m_filePath.isEmpty())
        m_filePath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                         .filePath("recent-projects.json");
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value("version").toInt() != 1) return;
    for (const auto& entry : object.value("directories").toArray()) {
        if (!entry.isString()) continue;
        const QString path = normalized(entry.toString());
        if (!path.isEmpty() && !m_paths.contains(path)) m_paths.push_back(path);
    }
}

bool RecentProjects::remember(const QString& path, QString* error) {
    const QString canonical = normalized(path);
    if (canonical.isEmpty()) return true;
    m_paths.removeAll(canonical);
    m_paths.prepend(canonical);
    while (m_paths.size() > maxRecentProjects) m_paths.removeLast();
    if (!QDir().mkpath(QFileInfo(m_filePath).absolutePath())) {
        if (error) *error = "Cannot create the local application data directory";
        return false;
    }
    QJsonArray directories;
    for (const auto& entry : m_paths) directories.push_back(entry);
    QSaveFile file(m_filePath);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"directories", directories}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace eudaimonia
