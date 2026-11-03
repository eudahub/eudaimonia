// SPDX-License-Identifier: GPL-3.0-only
#include "ProjectTemplates.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace eudaimonia {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("ProjectTemplates", text);
}

QString filled(QString text, const QString& name) {
    QString identifier = name;
    identifier.replace(QLatin1Char('-'), QLatin1Char('_'));
    text.replace(QLatin1String("@NAME@"), name);
    text.replace(QLatin1String("@IDENTIFIER@"), identifier);
    text.replace(QLatin1String("@LOWERNAME@"), name.toLower());
    return text;
}

} // namespace

const QList<ProjectTemplate>& projectTemplates() {
    static const QList<ProjectTemplate> templates{
        {QStringLiteral("cpp-console"), tr("C++ console program"), tr("CMake, one main.cpp."), QStringLiteral("main.cpp")},
        {QStringLiteral("cpp-qt"), tr("C++ program with a Qt window"), tr("CMake and Qt 6 Widgets: a main window with a menu."),
         QStringLiteral("MainWindow.cpp")},
        {QStringLiteral("rust-program"), tr("Rust program"), tr("Cargo, src/main.rs."), QStringLiteral("src/main.rs")},
        {QStringLiteral("rust-library"), tr("Rust library"), tr("Cargo, src/lib.rs with a test."), QStringLiteral("src/lib.rs")},
        {QStringLiteral("cpp-meson"), tr("C++ program with Meson"), tr("Meson, one main.cpp."), QStringLiteral("main.cpp")},
        {QStringLiteral("java-maven"), tr("Java program with Maven"), tr("pom.xml, src/main/java/app/Main.java."),
         QStringLiteral("src/main/java/app/Main.java")},
        {QStringLiteral("java-gradle"), tr("Java program with Gradle"), tr("build.gradle, src/main/java/app/Main.java."),
         QStringLiteral("src/main/java/app/Main.java")},
        {QStringLiteral("python"), tr("Python program"), tr("A directory with main.py."), QStringLiteral("main.py")},
        {QStringLiteral("javascript"), tr("JavaScript program"), tr("Node.js: package.json, index.js."), QStringLiteral("index.js")},
    };
    return templates;
}

bool isValidProjectName(const QString& name) {
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]*$"));
    return valid.match(name).hasMatch();
}

bool createProject(const ProjectTemplate& projectTemplate, const QString& directory, const QString& name, QString* error) {
    if (QFileInfo::exists(directory)) {
        *error = tr("%1 exists already.").arg(directory);
        return false;
    }
    const QString source = QStringLiteral(":/templates/") + projectTemplate.id;
    if (!QDir(source).exists()) {
        *error = tr("The template %1 is missing.").arg(projectTemplate.id);
        return false;
    }
    if (!QDir().mkpath(directory)) {
        *error = tr("Cannot make the directory %1.").arg(directory);
        return false;
    }
    const QDir sourceRoot(source);
    const QDir target(directory);
    auto fail = [&](const QString& why) {
        *error = why;
        QDir(directory).removeRecursively();
        return false;
    };
    QDirIterator files(source, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        QString relative = sourceRoot.relativeFilePath(files.filePath());
        relative.chop(3);  // ".in"
        if (QFileInfo(relative).fileName() == QLatin1String("gitignore"))
            relative = QFileInfo(relative).path() + QStringLiteral("/.gitignore");
        const QString path = QDir::cleanPath(target.filePath(relative));
        QFile in(files.filePath());
        if (!in.open(QIODevice::ReadOnly)) return fail(tr("Cannot read the template's %1.").arg(relative));
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return fail(tr("Cannot make the directory of %1.").arg(path));
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly)) return fail(tr("Cannot write %1: %2").arg(path, out.errorString()));
        out.write(filled(QString::fromUtf8(in.readAll()), name).toUtf8());
    }
    return true;
}

} // namespace eudaimonia
