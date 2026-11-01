// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace eudaimonia::lsp {

// How to start a language server and which files are its.
struct ServerSpec {
    QString name;               // e.g. "clangd"
    QString languages;          // for the user, e.g. "C and C++"
    QString program;            // found in PATH
    QStringList arguments;      // "${buildDir}" and "${projectDir}" are replaced
    QStringList suffixes;       // e.g. {"cpp", "h"}, without a dot, any case
    QStringList fileNames;      // e.g. {"CMakeLists.txt"}
    QString languageId;         // the protocol's, e.g. "cpp"; per suffix below
    QStringList languageIds;    // "suffix=id" pairs overriding languageId, e.g. "c=c"
    QString installHint;        // e.g. "sudo apt install clangd"

    bool matches(const QString& file) const;
    QString languageIdOf(const QString& file) const;
};

// The servers known: the built-in ones (clangd, neocmakelsp, rust-analyzer,
// pylsp), changed or added to by language-servers.json in the configuration
// directory and by the file named in EUDAIMONIA_LSP_SERVERS (for tests): a
// JSON array of objects with the fields above; one with a known name replaces
// that server, "program": "" removes it.
QList<ServerSpec> knownServers();
QList<ServerSpec> builtInServers();
// Applies a JSON array of server objects to `servers`.
void applyServerConfig(QList<ServerSpec>& servers, const QByteArray& json);

} // namespace eudaimonia::lsp
