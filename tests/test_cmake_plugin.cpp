// SPDX-License-Identifier: GPL-3.0-only
// The CMake plugin, loaded from its file as the IDE loads it, on a small
// project configured and built here (needs cmake and a C++ compiler; a few
// seconds).
#include <eudaimonia/plugin/BuildSystem.h>

#include <QDir>
#include <QFile>
#include <QPluginLoader>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using eudaimonia::BuildCommand;
using eudaimonia::BuildSystem;
using eudaimonia::BuildTarget;

class TestCMakePlugin : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_project;
    QString m_build;
    BuildSystem* m_cmake = nullptr;

    void write(const QString& name, const QByteArray& bytes) {
        QFile file(m_project.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
    }

    // Runs the command; its output goes into the failure message.
    static bool run(const BuildCommand& command) {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setWorkingDirectory(command.workingDirectory);
        process.start(command.program, command.arguments);
        const bool finished = process.waitForFinished(120000);
        if (!finished || process.exitCode() != 0) {
            qWarning("%s", process.readAll().constData());
            return false;
        }
        return true;
    }

    static const BuildTarget* find(const QList<BuildTarget>& targets, const QString& name) {
        for (const BuildTarget& target : targets)
            if (target.name == name) return &target;
        return nullptr;
    }

private slots:
    void initTestCase() {
        static QPluginLoader loader(QStringLiteral(CMAKE_PLUGIN_FILE));
        m_cmake = qobject_cast<BuildSystem*>(loader.instance());
        QVERIFY2(m_cmake, qPrintable(loader.errorString()));
        QVERIFY(m_project.isValid());
        write("CMakeLists.txt",
              "cmake_minimum_required(VERSION 3.16)\n"
              "project(sample LANGUAGES CXX)\n"
              "add_library(util STATIC util.cpp)\n"
              "add_executable(hello main.cpp)\n"
              "target_link_libraries(hello PRIVATE util)\n");
        write("util.cpp", "int answer() { return 42; }\n");
        write("main.cpp", "int answer();\nint main() { return answer() == 42 ? 0 : 1; }\n");
        m_build = m_project.filePath("build");
    }

    void recognizesCMakeProjects() {
        QCOMPARE(m_cmake->name(), QStringLiteral("CMake"));
        QVERIFY(m_cmake->recognizes(m_project.path()));
        QTemporaryDir empty;
        QVERIFY(!m_cmake->recognizes(empty.path()));
    }

    void noTargetsBeforeConfiguring() {
        QString error;
        QVERIFY(m_cmake->targets(m_build, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void readsTheTargetsAfterConfiguring() {
        const BuildCommand configure = m_cmake->configureCommand(m_project.path(), m_build);
        QCOMPARE(configure.program, QStringLiteral("cmake"));
        QCOMPARE(configure.arguments,
                 QStringList({QStringLiteral("-S"), m_project.path(), QStringLiteral("-B"), m_build}));
        QVERIFY(run(configure));

        QString error;
        const QList<BuildTarget> targets = m_cmake->targets(m_build, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const BuildTarget* hello = find(targets, QStringLiteral("hello"));
        const BuildTarget* util = find(targets, QStringLiteral("util"));
        QVERIFY(hello && util);
        QCOMPARE(hello->type, QStringLiteral("EXECUTABLE"));
        QCOMPARE(hello->executable, QDir(m_build).filePath(QStringLiteral("hello")));
        QCOMPARE(util->type, QStringLiteral("STATIC_LIBRARY"));
        QVERIFY(util->executable.isEmpty());
    }

    void buildsTheProgramWhereItSays() {
        qunsetenv("CMAKE_BUILD_PARALLEL_LEVEL");
        const QString jobs = QString::number(QThread::idealThreadCount());
        const BuildCommand build = m_cmake->buildCommand(m_build, QStringLiteral("hello"));
        QCOMPARE(build.arguments, QStringList({QStringLiteral("--build"), m_build, QStringLiteral("--parallel"), jobs,
                                               QStringLiteral("--target"), QStringLiteral("hello")}));
        QVERIFY(run(build));
        const QString program = find(m_cmake->targets(m_build, nullptr), QStringLiteral("hello"))->executable;
        QVERIFY(QFile::exists(program));
        QCOMPARE(QProcess::execute(program, {}), 0);
        QCOMPARE(m_cmake->buildCommand(m_build, {}).arguments,
                 QStringList({QStringLiteral("--build"), m_build, QStringLiteral("--parallel"), jobs}));
    }

    // The environment's parallel level wins over the processors' count.
    void leavesTheParallelLevelToTheEnvironment() {
        qputenv("CMAKE_BUILD_PARALLEL_LEVEL", "3");
        QCOMPARE(m_cmake->buildCommand(m_build, {}).arguments, QStringList({QStringLiteral("--build"), m_build}));
        qunsetenv("CMAKE_BUILD_PARALLEL_LEVEL");
    }
};

QTEST_GUILESS_MAIN(TestCMakePlugin)
#include "test_cmake_plugin.moc"
