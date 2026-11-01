// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "RecentProjects.h"

#include <QMainWindow>

class MruTabWidget;
class QFileSystemModel;
class QMenu;
class QSplitter;
class QTreeView;

namespace eudaimonia {

// The project directory as a tree on the left, open files in MRU tabs on the right.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Shows the directory in the tree and remembers it as a recent project.
    bool openProject(const QString& directory);
    // Opens the file in a tab, or switches to the tab already showing it.
    bool openFile(const QString& path);
    QString projectDirectory() const { return m_projectDirectory; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createMenus();
    void chooseProject();
    void chooseFile();
    void updateRecentProjectsMenu();
    void updateTitle();
    void restoreState();
    void saveState() const;

    QSplitter* m_splitter = nullptr;
    QTreeView* m_tree = nullptr;
    QFileSystemModel* m_model = nullptr;
    MruTabWidget* m_tabs = nullptr;
    QMenu* m_recentMenu = nullptr;
    QString m_projectDirectory;
    QString m_lastFileDirectory;
    RecentProjects m_recent;
};

} // namespace eudaimonia
