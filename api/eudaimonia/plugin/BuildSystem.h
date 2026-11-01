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

// A target of a configured project.
struct BuildTarget {
    QString name;
    // As the build tool names it, e.g. CMake's "EXECUTABLE",
    // "STATIC_LIBRARY", "SHARED_LIBRARY", "UTILITY".
    QString type;
    // The program built, for an executable: an absolute path; else empty.
    QString executable;
};

class BuildSystem {
public:
    virtual ~BuildSystem() = default;

    // For the user, e.g. "CMake".
    virtual QString name() const = 0;

    // The project directory is one this build system builds (e.g. it has a
    // CMakeLists.txt).
    virtual bool recognizes(const QString& projectDir) const = 0;

    // Prepares `buildDir` (e.g. asks the build tool to describe the project
    // there) and gives the command configuring the project into it.
    virtual BuildCommand configureCommand(const QString& projectDir, const QString& buildDir) = 0;

    // The targets of the project configured into `buildDir`, in the build
    // tool's order; empty with `error` set when they cannot be read (e.g. not
    // configured yet).
    virtual QList<BuildTarget> targets(const QString& buildDir, QString* error) const = 0;

    // The command building `target`, or everything when it is empty.
    virtual BuildCommand buildCommand(const QString& buildDir, const QString& target) const = 0;
};

} // namespace eudaimonia

#define EUDAIMONIA_BUILDSYSTEM_IID "io.github.eudahub.eudaimonia.BuildSystem/1"
Q_DECLARE_INTERFACE(eudaimonia::BuildSystem, EUDAIMONIA_BUILDSYSTEM_IID)

#endif // EUDAIMONIA_PLUGIN_BUILDSYSTEM_H
