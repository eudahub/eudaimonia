// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "PluginManager.h"
#include "RecentProjects.h"

#include <QMainWindow>

class MruTabWidget;
class QFileSystemModel;
class QListWidget;
class QMenu;
class QSplitter;
class QStackedWidget;
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
    void updateRecentProjects();
    void updateTitle();
    void showPlugins();
    void restoreState();
    void saveState() const;

    QSplitter* m_splitter = nullptr;
    // The recent projects until a project is opened, then the project tree
    QStackedWidget* m_side = nullptr;
    QListWidget* m_recentList = nullptr;
    QTreeView* m_tree = nullptr;
    QFileSystemModel* m_model = nullptr;
    MruTabWidget* m_tabs = nullptr;
    QMenu* m_recentMenu = nullptr;
    QString m_projectDirectory;
    QString m_lastFileDirectory;
    RecentProjects m_recent;
    PluginManager m_plugins;
};

} // namespace eudaimonia
