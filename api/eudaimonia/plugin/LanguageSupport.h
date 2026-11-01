// SPDX-License-Identifier: GPL-3.0-only
#ifndef EUDAIMONIA_PLUGIN_LANGUAGESUPPORT_H
#define EUDAIMONIA_PLUGIN_LANGUAGESUPPORT_H

// Interface of the language support plugin: language servers (the Language
// Server Protocol) for the project's languages — clangd for C and C++, a
// CMake server for CMakeLists.txt, rust-analyzer, ... — fed with the open
// documents, reporting their diagnostics and answering questions such as
// "where is this defined". Used on the GUI thread; answers come later, through
// the listener and callbacks.

#include <QList>
#include <QString>
#include <QStringList>
#include <QtPlugin>

#include <functional>

namespace eudaimonia {

// A language server the project needs.
struct LanguageServerInfo {
    QString name;          // e.g. "clangd"
    QString languages;     // for the user, e.g. "C and C++"
    bool installed = false;
    QString installHint;   // e.g. "sudo apt install clangd"
};

// A position in a document: line and column from 0, the column in UTF-16 code
// units (QString indexes), as the protocol counts by default.
struct SourcePosition {
    int line = 0;
    int column = 0;
};

struct SourceLocation {
    QString file;  // absolute path
    SourcePosition position;
};

struct Diagnostic {
    enum class Severity { Error = 1, Warning = 2, Information = 3, Hint = 4 };
    SourcePosition start;
    SourcePosition end;
    Severity severity = Severity::Error;
    QString message;
    QString source;  // e.g. "clang"
};

// Told what the servers report; the IDE implements what it needs.
class LanguageSupportListener {
public:
    virtual ~LanguageSupportListener() = default;
    // All the diagnostics of `file` now (an empty list clears them).
    virtual void diagnostics(const QString& file, const QList<Diagnostic>& list) { (void)file; (void)list; }
    // A message for the user, e.g. a server that stopped.
    virtual void message(const QString& text) { (void)text; }
};

class LanguageSupport {
public:
    virtual ~LanguageSupport() = default;

    // The servers whose languages the project has files of (by their names,
    // build directories and VCS data left out), installed or not.
    virtual QList<LanguageServerInfo> serversFor(const QString& projectDir) const = 0;

    // The project the documents belong to; the servers of the one before
    // stop. `buildDir` is where a build system wrote e.g. compile_commands.json.
    virtual void setProject(const QString& projectDir, const QString& buildDir) = 0;
    // Not owned; nullptr removes it.
    virtual void setListener(LanguageSupportListener* listener) = 0;

    // An installed server takes care of the file.
    virtual bool handles(const QString& file) const = 0;
    // The document's life in the editor: its whole text each time.
    virtual void opened(const QString& file, const QString& text) = 0;
    virtual void changed(const QString& file, const QString& text) = 0;
    virtual void saved(const QString& file) = 0;
    virtual void closed(const QString& file) = 0;

    // Where the symbol at `position` is defined; `done` gets none when the
    // server knows nothing (or there is no server).
    virtual void findDefinition(const QString& file, SourcePosition position,
                                std::function<void(const QList<SourceLocation>&)> done) = 0;

    // Stops every server (e.g. when the IDE closes).
    virtual void shutdown() = 0;
};

} // namespace eudaimonia

#define EUDAIMONIA_LANGUAGESUPPORT_IID "io.github.eudahub.eudaimonia.LanguageSupport/1"
Q_DECLARE_INTERFACE(eudaimonia::LanguageSupport, EUDAIMONIA_LANGUAGESUPPORT_IID)

#endif // EUDAIMONIA_PLUGIN_LANGUAGESUPPORT_H
