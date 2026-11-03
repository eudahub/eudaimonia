// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>

namespace eudaimonia {

// A kind of new project: its files are in the resources, under
// :/templates/<id>, each with ".in" after its name, so that tools looking at
// this repository do not take them for its own. In their text @NAME@ is the
// project's name, @IDENTIFIER@ that with '_' for '-', and @LOWERNAME@ in lower
// case; "gitignore.in" becomes ".gitignore".
struct ProjectTemplate {
    QString id;
    QString title;
    QString description;
    QString mainFile;  // opened in a tab, relative to the project
};

const QList<ProjectTemplate>& projectTemplates();

// A name fit for every kind: a letter, then letters, digits, '_' and '-'.
bool isValidProjectName(const QString& name);

// Makes the new directory `directory` (with what leads to it) and the files of
// the template in it. False, telling why, when the directory exists already or
// something cannot be written; what was made in it then goes.
bool createProject(const ProjectTemplate& projectTemplate, const QString& directory, const QString& name, QString* error);

} // namespace eudaimonia
