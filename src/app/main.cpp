// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QTextStream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Settings in ~/.config/eudahub/eudaimonia.conf, data in
    // ~/.local/share/eudahub/eudaimonia.
    QCoreApplication::setOrganizationName("eudahub");
    QCoreApplication::setApplicationName("eudaimonia");
    QCoreApplication::setApplicationVersion(EUDAIMONIA_VERSION);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/eudaimonia.svg")));
    // The launcher eudaimonia.desktop, e.g. for the icon on Wayland.
    QGuiApplication::setDesktopFileName(QStringLiteral("eudaimonia"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Integrated development environment");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("path", "Project directory or files to open", "[path...]");
    const QCommandLineOption listPlugins("list-plugins", "List the plugin directories and plugins, then exit");
    parser.addOption(listPlugins);
    parser.process(app);

    if (parser.isSet(listPlugins)) {
        QTextStream out(stdout);
        for (const QString& directory : eudaimonia::PluginManager::directories())
            out << "directory: " << directory << '\n';
        const eudaimonia::PluginManager plugins;
        for (const QString& line : plugins.report()) out << line << '\n';
        return 0;
    }

    eudaimonia::MainWindow window;
    window.show();
    for (const QString& path : parser.positionalArguments()) {
        if (QFileInfo(path).isDir())
            window.openProject(path);
        else
            window.openFile(path);
    }
    return app.exec();
}
