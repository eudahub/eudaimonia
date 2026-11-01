// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>

namespace eudaimonia {

// Runs commands in a terminal window of the desktop, for now the place of a
// program's console and of the build output. The window stays open when they
// end, with their exit status, until Enter is pressed.
class Terminal {
public:
    // The terminal used: the setting "run/terminal" when set, else
    // gnome-terminal when installed, else the one of the desktop (konsole,
    // xfce4-terminal, mate-terminal, qterminal; xterm otherwise).
    static QString program();

    // `script` is a bash script, run in `workingDirectory`. False with
    // `error` set when no terminal could be started.
    static bool run(const QString& script, const QString& workingDirectory, const QString& title,
                    QString* error);

    // `word` quoted for bash.
    static QString quote(const QString& word);
    // The words of a command quoted for bash and joined by spaces.
    static QString command(const QString& program, const QStringList& arguments);
};

} // namespace eudaimonia
