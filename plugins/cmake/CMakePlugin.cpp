// SPDX-License-Identifier: GPL-3.0-only
// The CMake build system: configures with "cmake -S <project> -B <build>",
// builds with "cmake --build <build> --parallel <logical processors>", and reads the targets from CMake's
// file API (https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html),
// which CMake writes when configuring, without parsing CMakeLists.txt.

#include <eudaimonia/plugin/BuildSystem.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStandardPaths>
#include <QThread>

namespace eudaimonia {
namespace {

// Our own query: CMake answers it in reply/ at every configuration.
const QString queryDir = QStringLiteral(".cmake/api/v1/query/client-eudaimonia");
const QString replyDir = QStringLiteral(".cmake/api/v1/reply");

QJsonObject readObject(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (!document.isObject()) {
        *error = QObject::tr("%1 is not valid JSON: %2").arg(path, parse.errorString());
        return {};
    }
    return document.object();
}

} // namespace

class CMakePlugin : public QObject, public BuildSystem {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EUDAIMONIA_BUILDSYSTEM_IID)
    Q_INTERFACES(eudaimonia::BuildSystem)

public:
    QString name() const override { return QStringLiteral("CMake"); }

    bool recognizes(const QString& projectDir) const override
    {
        return QFileInfo(QDir(projectDir).filePath(QStringLiteral("CMakeLists.txt"))).isFile();
    }

    QStringList backends() const override
    {
        // The generators whose build tool is installed.
        QStringList result;
        if (!QStandardPaths::findExecutable(QStringLiteral("ninja")).isEmpty()) result << QStringLiteral("Ninja");
        if (!QStandardPaths::findExecutable(QStringLiteral("make")).isEmpty())
            result << QStringLiteral("Unix Makefiles");
        return result;
    }

    QString configuredBackend(const QString& buildDir) const override
    {
        return cacheValue(buildDir, "CMAKE_GENERATOR");
    }

    // Ninja's job pools; make's -j counts every job alike.
    bool supportsLinkJobs(const QString& backend) const override
    {
        return backend.contains(QLatin1String("Ninja"));
    }

    BuildCommand configureCommand(const QString& projectDir, const QString& buildDir,
                                  const ConfigureOptions& options) override
    {
        const QString& backend = options.backend;
        // The query for the code model (targets and their files), answered
        // by the configuration that follows. Without it (e.g. a read-only
        // build directory) targets() reports that there is no code model.
        QDir(buildDir).mkpath(queryDir);
        QFile query(QDir(buildDir).filePath(queryDir + QStringLiteral("/codemodel-v2")));
        if (!query.exists() && query.open(QIODevice::WriteOnly)) query.close();
        QStringList arguments{QStringLiteral("-S"), projectDir, QStringLiteral("-B"), buildDir};
        const QString configured = configuredBackend(buildDir);
        if (!backend.isEmpty() && backend != configured) {
            // Another generator than the cache's needs a new cache (CMake 3.24).
            if (!configured.isEmpty()) arguments.prepend(QStringLiteral("--fresh"));
            arguments << QStringLiteral("-G") << backend;
        }
        // The links in a pool of their own (Ninja), set or cleared at every
        // configuration, so that the cache follows the option.
        const QString used = backend.isEmpty() ? configured : backend;
        if (supportsLinkJobs(used)) {
            if (options.linkJobs > 0)
                arguments << QStringLiteral("-DCMAKE_JOB_POOLS=eudaimonia_link=%1").arg(options.linkJobs)
                          << QStringLiteral("-DCMAKE_JOB_POOL_LINK=eudaimonia_link");
            else if (cacheValue(buildDir, "CMAKE_JOB_POOL_LINK") == QLatin1String("eudaimonia_link"))
                // Only our own pool: the user's pools stay.
                arguments << QStringLiteral("-UCMAKE_JOB_POOLS") << QStringLiteral("-UCMAKE_JOB_POOL_LINK");
        }
        return {QStringLiteral("cmake"), arguments, projectDir};
    }

    QList<BuildTarget> targets(const QString& buildDir, QString* error) const override
    {
        QString ignored;
        if (!error) error = &ignored;
        error->clear();
        const QDir reply(QDir(buildDir).filePath(replyDir));
        // The newest index names the newest answers.
        const QStringList indexes =
            reply.entryList({QStringLiteral("index-*.json")}, QDir::Files, QDir::Name);
        if (indexes.isEmpty()) {
            *error = QObject::tr("%1 is not configured by CMake yet.").arg(buildDir);
            return {};
        }
        const QJsonObject index = readObject(reply.filePath(indexes.last()), error);
        if (!error->isEmpty()) return {};
        QString codemodelFile;
        const QJsonObject ours = index.value(QStringLiteral("reply")).toObject()
                                     .value(QStringLiteral("client-eudaimonia")).toObject();
        codemodelFile = ours.value(QStringLiteral("codemodel-v2")).toObject()
                            .value(QStringLiteral("jsonFile")).toString();
        if (codemodelFile.isEmpty()) {
            *error = QObject::tr("CMake gave no code model in %1; is it older than 3.14?").arg(buildDir);
            return {};
        }
        const QJsonObject codemodel = readObject(reply.filePath(codemodelFile), error);
        if (!error->isEmpty()) return {};
        // Artifacts are relative to the top of the build tree.
        const QDir buildTop(codemodel.value(QStringLiteral("paths")).toObject()
                                .value(QStringLiteral("build")).toString(buildDir));
        // One configuration, or the first of a multi-configuration generator.
        const QJsonArray configurations = codemodel.value(QStringLiteral("configurations")).toArray();
        if (configurations.isEmpty()) return {};
        QList<BuildTarget> result;
        for (const QJsonValue& entry : configurations.first().toObject().value(QStringLiteral("targets")).toArray()) {
            const QJsonObject summary = entry.toObject();
            BuildTarget target;
            target.name = summary.value(QStringLiteral("name")).toString();
            const QJsonObject details =
                readObject(reply.filePath(summary.value(QStringLiteral("jsonFile")).toString()), error);
            if (!error->isEmpty()) return {};
            target.type = details.value(QStringLiteral("type")).toString();
            const QJsonArray artifacts = details.value(QStringLiteral("artifacts")).toArray();
            if (target.type == QLatin1String("EXECUTABLE") && !artifacts.isEmpty())
                target.executable = QDir::cleanPath(
                    buildTop.absoluteFilePath(artifacts.first().toObject().value(QStringLiteral("path")).toString()));
            result << target;
        }
        return result;
    }

    BuildCommand buildCommand(const QString& buildDir, const QString& target) const override
    {
        QStringList arguments{QStringLiteral("--build"), buildDir};
        // As many jobs as logical processors: "--parallel" alone would leave
        // make without a limit. CMAKE_BUILD_PARALLEL_LEVEL, when set, decides.
        if (qEnvironmentVariableIsEmpty("CMAKE_BUILD_PARALLEL_LEVEL"))
            arguments << QStringLiteral("--parallel") << QString::number(QThread::idealThreadCount());
        if (!target.isEmpty()) arguments << QStringLiteral("--target") << target;
        return {QStringLiteral("cmake"), arguments, buildDir};
    }

private:
    // The value of `name` in buildDir's CMakeCache.txt; empty without it.
    static QString cacheValue(const QString& buildDir, const QByteArray& name)
    {
        QFile cache(QDir(buildDir).filePath(QStringLiteral("CMakeCache.txt")));
        if (!cache.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        const QByteArray prefix = name + ':';
        while (!cache.atEnd()) {
            const QByteArray line = cache.readLine().trimmed();
            const qsizetype equals = line.indexOf('=');
            if (line.startsWith(prefix) && equals > 0) return QString::fromUtf8(line.mid(equals + 1));
        }
        return {};
    }
};

} // namespace eudaimonia

#include "CMakePlugin.moc"
