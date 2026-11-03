// SPDX-License-Identifier: GPL-3.0-only
// The Cargo plugin, loaded from its file as the IDE loads it, on small projects
// written here; one small program is really built. Needs cargo, not the network.
#include <eudaimonia/plugin/BuildSystem.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace eudaimonia;

namespace {

void write(const QTemporaryDir& directory, const QString& name, const QByteArray& bytes) {
    const QString path = directory.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(bytes);
}

const BuildTarget* find(const QList<BuildTarget>& targets, const QString& name) {
    for (const BuildTarget& target : targets)
        if (target.name == name) return &target;
    return nullptr;
}

const QByteArray package = "[package]\nname = \"sample\"\nversion = \"0.1.0\"\nedition = \"2021\"\n";

} // namespace

class TestCargoPlugin : public QObject {
    Q_OBJECT

private:
    BuildSystem* m_cargo = nullptr;
    QTemporaryDir m_project;
    QString m_debug;

    static bool run(const BuildCommand& command) {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setWorkingDirectory(command.workingDirectory);
        process.start(command.program, command.arguments);
        const bool finished = process.waitForFinished(60000);
        if (!finished || process.exitCode() != 0) qWarning("%s", process.readAll().constData());
        return finished && process.exitCode() == 0;
    }

private slots:
    void initTestCase() {
        if (QStandardPaths::findExecutable(QStringLiteral("cargo")).isEmpty()) QSKIP("cargo is not installed");
        static QPluginLoader loader(QStringLiteral(CARGO_PLUGIN_FILE));
        m_cargo = qobject_cast<BuildSystem*>(loader.instance());
        QVERIFY2(m_cargo, qPrintable(loader.errorString()));
        qunsetenv("CARGO_TARGET_DIR");
        QVERIFY(m_project.isValid());
        write(m_project, QStringLiteral("Cargo.toml"), package);
        write(m_project, QStringLiteral("src/main.rs"), "fn main() {}\n");
        write(m_project, QStringLiteral("src/lib.rs"), "pub fn f() {}\n");
        write(m_project, QStringLiteral("examples/ex.rs"), "fn main() {}\n");
        write(m_project, QStringLiteral("tests/check.rs"), "#[test]\nfn t() {}\n");
        m_debug = m_project.filePath(QStringLiteral("target/debug"));
    }

    void recognizesCargoProjects() {
        QCOMPARE(m_cargo->name(), QStringLiteral("Cargo"));
        QVERIFY(m_cargo->recognizes(m_project.path()));
        QTemporaryDir empty;
        QVERIFY(!m_cargo->recognizes(empty.path()));
        QVERIFY(!m_cargo->canDebugConfigure());
        QCOMPARE(m_cargo->backends(), QStringList{QStringLiteral("cargo")});
        QVERIFY(!m_cargo->supportsLinkJobs(QStringLiteral("cargo")));
    }

    void buildsInCargosOwnDirectories() {
        QCOMPARE(m_cargo->buildDirectory(m_project.path(), {}), m_debug);
        QCOMPARE(m_cargo->buildDirectory(m_project.path(), QStringLiteral("Debug")), m_debug);
        QCOMPARE(m_cargo->buildDirectory(m_project.path(), QStringLiteral("Release")),
                 m_project.filePath(QStringLiteral("target/release")));
    }

    void readsItsTargets() {
        QString error;
        const QList<BuildTarget> targets = m_cargo->targets(m_debug, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const BuildTarget* program = find(targets, QStringLiteral("sample"));
        // The library and the program share the package's name: the program counts.
        for (const BuildTarget& target : targets)
            if (target.name == QLatin1String("sample") && !target.executable.isEmpty()) program = &target;
        const BuildTarget* example = find(targets, QStringLiteral("ex"));
        QVERIFY(program && example);
        QCOMPARE(program->executable, QDir(m_debug).filePath(QStringLiteral("sample")));
        QCOMPARE(example->executable, QDir(m_debug).filePath(QStringLiteral("examples/ex")));
        // They run where "cargo run" runs them.
        QCOMPARE(program->runDirectory, m_project.path());
        QCOMPARE(example->runDirectory, m_project.path());
        QVERIFY(!find(targets, QStringLiteral("check")));  // tests are no targets to run
    }

    void givesItsCommands() {
        const QString jobs = QStringLiteral("--jobs");
        const BuildCommand example = m_cargo->buildCommand(m_debug, QStringLiteral("ex"));
        QCOMPARE(example.program, QStringLiteral("cargo"));
        QCOMPARE(example.workingDirectory, m_project.path());
        QCOMPARE(example.arguments, QStringList({QStringLiteral("build"), QStringLiteral("--package"), QStringLiteral("sample"),
                                                 QStringLiteral("--example"), QStringLiteral("ex")}));
        BuildOptions options;
        options.jobs = 3;
        QCOMPARE(m_cargo->buildCommand(m_project.filePath(QStringLiteral("target/release")), {}, options).arguments,
                 QStringList({QStringLiteral("build"), QStringLiteral("--release"), jobs, QStringLiteral("3"), QStringLiteral("--workspace")}));
        // Cargo cleans in a command of its own, of this profile only.
        options.clean = true;
        const BuildCommand rebuild = m_cargo->buildCommand(m_debug, {}, options);
        QCOMPARE(rebuild.program, QStringLiteral("sh"));
        QVERIFY(rebuild.arguments.at(1).startsWith(QLatin1String("cargo clean --profile dev && ")));
        QVERIFY(rebuild.arguments.contains(QStringLiteral("--workspace")));
    }

    // Offline: the project has no dependencies to fetch.
    void buildsAndRebuildsTheProgram() {
        BuildOptions options;
        options.clean = true;
        QVERIFY(run(m_cargo->buildCommand(m_debug, {}, options)));
        const QString program = QDir(m_debug).filePath(QStringLiteral("sample"));
        QVERIFY(QFile::exists(program));
        QCOMPARE(QProcess::execute(program, {}), 0);
    }

    // Configuring checks the manifest, and the targets are read anew.
    void tellsWhatIsWrongWithTheManifest() {
        QTemporaryDir project;
        write(project, QStringLiteral("Cargo.toml"), package);
        write(project, QStringLiteral("src/main.rs"), "fn main() {}\n");
        const QString debug = m_cargo->buildDirectory(project.path(), {});
        QVERIFY(find(m_cargo->targets(debug, nullptr), QStringLiteral("sample")));

        write(project, QStringLiteral("Cargo.toml"), "not a manifest");
        QVERIFY(!run(m_cargo->configureCommand(project.path(), debug, {})));
        QString error;
        QVERIFY(m_cargo->targets(debug, &error).isEmpty());
        QVERIFY(!error.isEmpty());

        write(project, QStringLiteral("Cargo.toml"), package);
        QVERIFY(run(m_cargo->configureCommand(project.path(), debug, {})));
        QVERIFY(find(m_cargo->targets(debug, &error), QStringLiteral("sample")));
    }

    // A member opened as the project: everything is in the workspace's root,
    // but it runs in the member, as "cargo run" there.
    void buildsAWorkspace() {
        QTemporaryDir root;
        write(root, QStringLiteral("Cargo.toml"), "[workspace]\nmembers = [\"one\", \"two\"]\nresolver = \"2\"\n");
        for (const char* member : {"one", "two"}) {
            const QString name = QString::fromLatin1(member);
            write(root, name + QStringLiteral("/Cargo.toml"),
                  QByteArray("[package]\nname = \"") + member + "\"\nversion = \"0.1.0\"\nedition = \"2021\"\n");
            write(root, name + QStringLiteral("/src/main.rs"), "fn main() {}\n");
        }
        const QString debug = m_cargo->buildDirectory(root.path(), {});
        QCOMPARE(debug, root.filePath(QStringLiteral("target/debug")));
        const QList<BuildTarget> targets = m_cargo->targets(debug, nullptr);
        QVERIFY(find(targets, QStringLiteral("one")) && find(targets, QStringLiteral("two")));
        const BuildCommand two = m_cargo->buildCommand(debug, QStringLiteral("two"));
        QVERIFY(two.arguments.contains(QStringLiteral("--package")));
        QCOMPARE(two.workingDirectory, root.path());

        const QString member = root.filePath(QStringLiteral("one"));
        const QString fromMember = m_cargo->buildDirectory(member, {});
        QCOMPARE(fromMember, debug);
        QCOMPARE(m_cargo->buildCommand(fromMember, QStringLiteral("one")).workingDirectory, root.path());
        QCOMPARE(find(m_cargo->targets(fromMember, nullptr), QStringLiteral("one"))->runDirectory, member);
    }

    void followsCargoTargetDir() {
        QTemporaryDir project;
        QTemporaryDir elsewhere;
        write(project, QStringLiteral("Cargo.toml"), package);
        write(project, QStringLiteral("src/main.rs"), "fn main() {}\n");
        qputenv("CARGO_TARGET_DIR", elsewhere.path().toUtf8());
        const QString debug = m_cargo->buildDirectory(project.path(), {});
        qunsetenv("CARGO_TARGET_DIR");
        QCOMPARE(debug, QDir(elsewhere.path()).filePath(QStringLiteral("debug")));
        // A build directory outside the project still knows its project.
        QCOMPARE(find(m_cargo->targets(debug, nullptr), QStringLiteral("sample"))->executable,
                 QDir(debug).filePath(QStringLiteral("sample")));
    }
};

QTEST_GUILESS_MAIN(TestCargoPlugin)
#include "test_cargo_plugin.moc"
