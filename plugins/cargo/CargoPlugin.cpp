// SPDX-License-Identifier: GPL-3.0-only
// The Cargo build system, for Rust projects: there is nothing to configure, so
// "configuring" is Cargo checking its manifest; it builds with "cargo build"
// into its own target/debug and target/release, and the targets come from
// "cargo metadata", without parsing Cargo.toml.

#include <eudaimonia/plugin/BuildSystem.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>

namespace eudaimonia {
namespace {

const QString cargo = QStringLiteral("cargo");
const QString manifest = QStringLiteral("Cargo.toml");
const QStringList metadataArguments{QStringLiteral("metadata"), QStringLiteral("--format-version"), QStringLiteral("1"),
                                    QStringLiteral("--no-deps")};
constexpr int metadataTimeoutMs = 30000;

bool isExample(const QStringList& kinds) { return kinds.contains(QLatin1String("example")); }
bool isProgram(const QStringList& kinds) { return kinds.contains(QLatin1String("bin")) || isExample(kinds); }
bool isLibrary(const QStringList& kinds) {
    for (const QString& kind : kinds)
        if (kind.endsWith(QLatin1String("lib")) || kind == QLatin1String("staticlib") || kind == QLatin1String("proc-macro"))
            return true;
    return false;
}

} // namespace

class CargoPlugin : public QObject, public BuildSystem {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EUDAIMONIA_BUILDSYSTEM_IID)
    Q_INTERFACES(eudaimonia::BuildSystem)

public:
    QString name() const override { return QStringLiteral("Cargo"); }

    bool recognizes(const QString& projectDir) const override { return QFileInfo(QDir(projectDir).filePath(manifest)).isFile(); }

    QStringList backends() const override
    {
        return QStandardPaths::findExecutable(cargo).isEmpty() ? QStringList() : QStringList{cargo};
    }

    QString configuredBackend(const QString&) const override { return cargo; }
    bool supportsLinkJobs(const QString&) const override { return false; }
    bool canDebugConfigure() const override { return false; }

    // Cargo's own places: the workspace's target directory (or wherever
    // CARGO_TARGET_DIR says), debug for all but Release.
    QString buildDirectory(const QString& projectDir, const QString& buildType) const override
    {
        QString error;
        const Metadata& known = metadata(projectDir, &error);
        const QString target = known.targetDirectory.isEmpty() ? QDir(projectDir).filePath(QStringLiteral("target"))
                                                               : known.targetDirectory;
        const QString directory =
            QDir(target).filePath(buildType == QLatin1String("Release") ? QStringLiteral("release") : QStringLiteral("debug"));
        m_projects.insert(directory, projectDir);
        return directory;
    }

    // Checks the manifest, which tells what is wrong with it; the targets are
    // read anew afterwards.
    BuildCommand configureCommand(const QString& projectDir, const QString&, const ConfigureOptions&) override
    {
        m_metadata.remove(projectDir);
        return {cargo, metadataArguments, projectDir};
    }

    QList<BuildTarget> targets(const QString& buildDir, QString* error) const override
    {
        QString ignored;
        if (!error) error = &ignored;
        error->clear();
        QList<BuildTarget> result;
        for (const Target& target : metadata(projectOf(buildDir), error).targets) {
            QString executable;
            if (isProgram(target.kinds))
                executable = QDir(buildDir).filePath(isExample(target.kinds) ? QStringLiteral("examples/") + target.name : target.name);
            else if (!isLibrary(target.kinds))
                continue;  // tests, benchmarks, build scripts
            result.append({target.name, target.kinds.value(0), executable});
        }
        return result;
    }

    BuildCommand buildCommand(const QString& buildDir, const QString& target, const BuildOptions& options) const override
    {
        const bool release = QFileInfo(buildDir).fileName() == QLatin1String("release");
        QString error;
        const Metadata& known = metadata(projectOf(buildDir), &error);
        QStringList arguments{QStringLiteral("build")};
        if (release) arguments << QStringLiteral("--release");
        if (options.jobs > 0) arguments << QStringLiteral("--jobs") << QString::number(options.jobs);
        if (target.isEmpty()) arguments << QStringLiteral("--workspace");
        for (const Target& one : known.targets) {
            if (one.name != target) continue;
            arguments << QStringLiteral("--package") << one.package;
            if (isExample(one.kinds)) arguments << QStringLiteral("--example") << target;
            else if (isProgram(one.kinds)) arguments << QStringLiteral("--bin") << target;
            else arguments << QStringLiteral("--lib");
            break;
        }
        // Messages name files relative to the workspace.
        const QString directory = known.workspaceRoot.isEmpty() ? projectOf(buildDir) : known.workspaceRoot;
        if (!options.clean) return {cargo, arguments, directory};
        // Cargo has no "clean first": what this profile built goes, then the build.
        const QString clean = release ? QStringLiteral("cargo clean --release") : QStringLiteral("cargo clean --profile dev");
        return {QStringLiteral("sh"),
                QStringList{QStringLiteral("-c"), clean + QStringLiteral(" && exec cargo \"$@\""), QStringLiteral("sh")} + arguments,
                directory};
    }

private:
    struct Target {
        QString name;
        QString package;
        QStringList kinds;
    };
    struct Metadata {
        QDateTime manifestTime;
        QString targetDirectory;
        QString workspaceRoot;
        QList<Target> targets;
    };

    // What Cargo says of the project, until its manifest changes or the
    // project is configured: this is asked for often, and Cargo takes a moment.
    const Metadata& metadata(const QString& projectDir, QString* error) const
    {
        const QDateTime time = QFileInfo(QDir(projectDir).filePath(manifest)).lastModified();
        const auto known = m_metadata.constFind(projectDir);
        if (known != m_metadata.constEnd() && known->manifestTime == time) return *known;
        Metadata read;
        QProcess process;
        process.setWorkingDirectory(projectDir);
        process.start(cargo, metadataArguments);
        if (!process.waitForFinished(metadataTimeoutMs) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            const QString why = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            *error = why.isEmpty() ? QObject::tr("%1 cannot be run: %2").arg(cargo, process.errorString()) : why;
            process.kill();
            process.waitForFinished(1000);
            m_failed = {};
            return m_failed;  // not kept: the next look may find it mended
        }
        const QJsonObject root = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        read.manifestTime = time;
        read.targetDirectory = root.value(QStringLiteral("target_directory")).toString();
        read.workspaceRoot = root.value(QStringLiteral("workspace_root")).toString();
        for (const QJsonValue& package : root.value(QStringLiteral("packages")).toArray()) {
            const QString packageName = package.toObject().value(QStringLiteral("name")).toString();
            for (const QJsonValue& value : package.toObject().value(QStringLiteral("targets")).toArray()) {
                const QJsonObject target = value.toObject();
                QStringList kinds;
                for (const QJsonValue& kind : target.value(QStringLiteral("kind")).toArray()) kinds << kind.toString();
                read.targets.append({target.value(QStringLiteral("name")).toString(), packageName, kinds});
            }
        }
        return *m_metadata.insert(projectDir, read);
    }

    // The project a build directory is of: said by buildDirectory(), else the
    // nearest manifest above it.
    QString projectOf(const QString& buildDir) const
    {
        const QString known = m_projects.value(buildDir);
        if (!known.isEmpty()) return known;
        QDir directory(buildDir);
        while (directory.cdUp())
            if (directory.exists(manifest)) return directory.path();
        return buildDir;
    }

    mutable QHash<QString, Metadata> m_metadata;  // by project
    mutable QHash<QString, QString> m_projects;   // by build directory
    mutable Metadata m_failed;
};

} // namespace eudaimonia

#include "CargoPlugin.moc"
