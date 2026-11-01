// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Settings in ~/.config/eudahub/eudaimonia.conf, data in
    // ~/.local/share/eudahub/eudaimonia.
    QCoreApplication::setOrganizationName("eudahub");
    QCoreApplication::setApplicationName("eudaimonia");
    QCoreApplication::setApplicationVersion(EUDAIMONIA_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("Integrated development environment");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("path", "Project directory or files to open", "[path...]");
    parser.process(app);

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
