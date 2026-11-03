// SPDX-License-Identifier: GPL-3.0-only
#ifndef EUDAIMONIA_PLUGIN_BUILDSYSTEM_H
#define EUDAIMONIA_PLUGIN_BUILDSYSTEM_H

// Interface of a build system plugin (CMake, later Meson and others): it
// knows how to configure and build a project and which targets it has; the
// IDE runs the commands it gives (in a terminal, later with their output
// captured). Used on the GUI thread; calls do not block on the build tool.

#include <QList>
#include <QString>
#include <QStringList>
#include <QtPlugin>

namespace eudaimonia {

// A command for the IDE to run.
struct BuildCommand {
    QString program;
    QStringList arguments;
    QString workingDirectory;
};

// How the IDE wants a project configured.
struct ConfigureOptions {
    // The tool doing the building (backends()); empty keeps the one the
    // build directory was configured with, the build tool's default for a
    // new one. Another than that one configures the directory afresh, after
    // which everything is built again.
    QString backend;
    // At most this many links at once (they take much memory), while
    // compiling uses all jobs; 0: no limit of its own. Only build tools
    // that can (supportsLinkJobs()).
    int linkJobs = 0;
    // "Debug", "Release"; empty leaves it to the project and to what the
    // build directory has.
    QString buildType;
};

// How the IDE wants a project built.
struct BuildOptions {
    // Jobs at once; 0: the build system's choice (for CMake as many as
    // logical processors, unless CMAKE_BUILD_PARALLEL_LEVEL is set).
    int jobs = 0;
    // What was built goes first: everything is built anew.
    bool clean = false;
};

// A target of a configured project.
struct BuildTarget {
    QString name;
    // As the build tool names it, e.g. CMake's "EXECUTABLE",
    // "STATIC_LIBRARY", "SHARED_LIBRARY", "UTILITY".
    QString type;
    // The program built, for an executable: an absolute path; else empty.
    QString executable;
    // Where the program runs, as the build tool's own run does it; empty: in
    // the directory of the program.
    QString runDirectory;
};

class BuildSystem {
public:
    virtual ~BuildSystem() = default;

    // For the user, e.g. "CMake".
    virtual QString name() const = 0;

    // The project directory is one this build system builds (e.g. it has a
    // CMakeLists.txt).
    virtual bool recognizes(const QString& projectDir) const = 0;

    // Where a project is built with a build type ("Debug", "Release", or
    // empty: the project's own choice), e.g. CMake's <project>/build-debug.
    virtual QString buildDirectory(const QString& projectDir, const QString& buildType) const = 0;
    // Configuring itself can be debugged (DebugRequest::Kind::CMakeConfigure).
    virtual bool canDebugConfigure() const = 0;

    // The tools that can do the building, installed here, the preferred
    // first, e.g. CMake's generators {"Ninja", "Unix Makefiles"}.
    virtual QStringList backends() const = 0;
    // The tool `buildDir` was configured with; empty when it was not
    // configured yet.
    virtual QString configuredBackend(const QString& buildDir) const = 0;

    // The backend can limit the links running at once (ConfigureOptions::linkJobs).
    virtual bool supportsLinkJobs(const QString& backend) const = 0;

    // Prepares `buildDir` (e.g. asks the build tool to describe the project
    // there) and gives the command configuring the project into it.
    virtual BuildCommand configureCommand(const QString& projectDir, const QString& buildDir,
                                          const ConfigureOptions& options) = 0;

    // The targets of the project configured into `buildDir`, in the build
    // tool's order; empty with `error` set when they cannot be read (e.g. not
    // configured yet).
    virtual QList<BuildTarget> targets(const QString& buildDir, QString* error) const = 0;

    // The command building `target`, or everything when it is empty.
    virtual BuildCommand buildCommand(const QString& buildDir, const QString& target,
                                      const BuildOptions& options = {}) const = 0;
};

} // namespace eudaimonia

#define EUDAIMONIA_BUILDSYSTEM_IID "io.github.eudahub.eudaimonia.BuildSystem/6"
Q_DECLARE_INTERFACE(eudaimonia::BuildSystem, EUDAIMONIA_BUILDSYSTEM_IID)

#endif // EUDAIMONIA_PLUGIN_BUILDSYSTEM_H
