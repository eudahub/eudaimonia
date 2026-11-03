// SPDX-License-Identifier: GPL-3.0-only
#include "CompilerMessages.h"

#include <QRegularExpression>

namespace eudaimonia {

std::optional<CompilerMessage> parseCompilerMessage(const QString& line, const QString& previousLine) {
    // rustc's " --> file:line:column" under its message, and " ::: ..." of
    // other places it names.
    static const QRegularExpression rust(QStringLiteral(R"(^\s*(?:-->|:::)\s+(.+):(\d+):(\d+)\s*$)"));
    const QRegularExpressionMatch place = rust.match(line);
    if (place.hasMatch()) {
        CompilerMessage result;
        result.file = place.captured(1);
        result.line = place.captured(2).toInt();
        result.column = place.captured(3).toInt();
        result.kind = previousLine.startsWith(QLatin1String("error"))     ? CompilerMessage::Kind::Error
                      : previousLine.startsWith(QLatin1String("warning")) ? CompilerMessage::Kind::Warning
                                                                          : CompilerMessage::Kind::Note;
        return result;
    }
    // file:line[:column]: [fatal ]error|warning|note: text; also the places
    // of "In file included from a.h:3," and of the linker's
    // "a.cpp:5:(.text+0x9): undefined reference" (a linker's name before it
    // is cut off below).
    static const QRegularExpression message(
        QStringLiteral(R"(^(?:In file included from |\s+from )?([^\s:][^:]*):(\d+)(?::(\d+))?:?\s*)"
                       R"((?:(fatal error|error|warning|note)\b|$|,|\())"));
    // "/usr/bin/ld: path:line: undefined reference": the linker's name first.
    QString text = line;
    static const QRegularExpression linker(QStringLiteral(R"(^\S*\bld(?:\.\w+)?:\s+)"));
    const QRegularExpressionMatch tool = linker.match(text);
    if (tool.hasMatch()) text = text.mid(tool.capturedLength());

    const QRegularExpressionMatch match = message.match(text);
    if (!match.hasMatch()) return std::nullopt;
    CompilerMessage result;
    result.file = match.captured(1);
    result.line = match.captured(2).toInt();
    result.column = match.captured(3).toInt();
    const QString kind = match.captured(4);
    if (kind == QLatin1String("warning")) result.kind = CompilerMessage::Kind::Warning;
    else if (kind == QLatin1String("note") || kind.isEmpty()) result.kind = CompilerMessage::Kind::Note;
    // A linker's line without a kind is an error (undefined reference).
    if (tool.hasMatch() && kind.isEmpty()) result.kind = CompilerMessage::Kind::Error;
    if (result.line <= 0) return std::nullopt;
    return result;
}

} // namespace eudaimonia
