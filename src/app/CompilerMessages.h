// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

#include <optional>

namespace eudaimonia {

// A place in a file named by a line of build output, as GCC, Clang and the
// linkers write it: "src/a.cpp:12:5: error: ...", "a.cpp:12: warning: ...",
// "In file included from a.h:3:" or "/usr/bin/ld: x.o: in function ...".
struct CompilerMessage {
    enum class Kind { Error, Warning, Note };
    Kind kind = Kind::Error;
    QString file;     // as written; relative to the directory the tool ran in
    int line = 0;     // from 1
    int column = 0;   // from 1; 0 when not given
};

// The message of one line of build output; none for other lines.
std::optional<CompilerMessage> parseCompilerMessage(const QString& line);

} // namespace eudaimonia
