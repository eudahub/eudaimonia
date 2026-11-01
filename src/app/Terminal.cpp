// SPDX-License-Identifier: GPL-3.0-only
#include "Terminal.h"

#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace eudaimonia {
namespace {

// The desktop environment's name, e.g. "GNOME", "KDE", "XFCE".
QString desktopEnvironment() {
    for (const char* variable : {"XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "DESKTOP_SESSION"}) {
        const QString value = qEnvironmentVariable(variable);
        if (!value.isEmpty()) return value;
    }
    return {};
}

// As gemini-commander's DistroInfo::suggestedTerminal().
QString desktopTerminal() {
    const QString desktop = desktopEnvironment().toLower();
    if (desktop.contains("gnome") || desktop.contains("cinnamon") || desktop.contains("unity"))
        return QStringLiteral("gnome-terminal");
    if (desktop.contains("kde") || desktop.contains("plasma")) return QStringLiteral("konsole");
    if (desktop.contains("xfce")) return QStringLiteral("xfce4-terminal");
    if (desktop.contains("mate")) return QStringLiteral("mate-terminal");
    if (desktop.contains("lxqt") || desktop.contains("lxde")) return QStringLiteral("qterminal");
    return QStringLiteral("xterm");
}

} // namespace

QString Terminal::program() {
    const QString chosen = QSettings().value(QStringLiteral("run/terminal")).toString();
    if (!chosen.isEmpty()) return chosen;
    if (!QStandardPaths::findExecutable(QStringLiteral("gnome-terminal")).isEmpty())
        return QStringLiteral("gnome-terminal");
    return desktopTerminal();
}

QString Terminal::quote(const QString& word) {
    QString quoted = word;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QString Terminal::command(const QString& program, const QStringList& arguments) {
    QStringList words{quote(program)};
    for (const QString& argument : arguments) words << quote(argument);
    return words.join(QLatin1Char(' '));
}

bool Terminal::run(const QString& script, const QString& workingDirectory, const QString& title,
                   QString* error) {
    const QString terminal = program();
    if (QStandardPaths::findExecutable(terminal).isEmpty()) {
        if (error) *error = QObject::tr("The terminal %1 is not installed.").arg(terminal);
        return false;
    }
    // The window stays open with the exit status of the last command.
    const QString held = script
                         + QStringLiteral("\nstatus=$?\necho\necho \"[exit $status]\"\n"
                                          "read -r -p 'Press Enter to close this window' _\n");
    const QStringList bash{QStringLiteral("bash"), QStringLiteral("-c"), held};
    const QString name = QFileInfo(terminal).fileName();
    QStringList arguments;
    if (name == QLatin1String("gnome-terminal")) {
        arguments << QStringLiteral("--title=%1").arg(title) << QStringLiteral("--") << bash;
    } else if (name == QLatin1String("konsole")) {
        arguments << QStringLiteral("-p") << QStringLiteral("tabtitle=%1").arg(title) << QStringLiteral("-e")
                  << bash;
    } else if (name == QLatin1String("xfce4-terminal") || name == QLatin1String("mate-terminal")) {
        arguments << QStringLiteral("--title=%1").arg(title) << QStringLiteral("-x") << bash;
    } else if (name == QLatin1String("qterminal")) {
        // qterminal takes the command as one string.
        arguments << QStringLiteral("-e") << command(bash.first(), bash.mid(1));
    } else {
        arguments << QStringLiteral("-T") << title << QStringLiteral("-e") << bash;
    }
    if (!QProcess::startDetached(terminal, arguments, workingDirectory)) {
        if (error) *error = QObject::tr("The terminal %1 could not be started.").arg(terminal);
        return false;
    }
    return true;
}

} // namespace eudaimonia
