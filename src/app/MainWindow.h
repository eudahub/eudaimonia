// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "PluginManager.h"
#include "RecentProjects.h"

#include <QMainWindow>

#include <memory>
#include <unordered_map>

class MruTabWidget;
class QFileSystemModel;
class QLabel;
class QListWidget;
class QMenu;
class QSplitter;
class QStackedWidget;
class QTreeView;

namespace spp {
class EditorPlugin;
class EditorView;
}

namespace eudaimonia {

// The project directory as a tree on the left, open files in MRU tabs on the
// right, each in an editor of the editor plugin (spp-editor).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Shows the directory in the tree and remembers it as a recent project.
    bool openProject(const QString& directory);
    // Opens the file in a tab, or switches to the tab already showing it.
    bool openFile(const QString& path);
    QString projectDirectory() const { return m_projectDirectory; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    struct Editor;

    void createMenus();
    void chooseProject();
    void chooseFile();
    void updateRecentProjects();
    void updateTitle();
    void showPlugins();
    void restoreState();
    void saveState() const;

    // The editor plugin, or null after telling the user it is missing.
    spp::EditorPlugin* editorPlugin();
    spp::EditorView* viewOf(QWidget* page) const;
    // Saves the tab's file; `askName` (or a document without a file) asks
    // for a name first. False when not saved.
    bool saveTab(QWidget* page, bool askName = false);
    // Asks about unsaved edits before the tab closes: save, discard or keep.
    bool confirmClose(QWidget* page);
    // Files changed on disk: unchanged tabs are reloaded, a modified one asks.
    void checkExternalChanges();
    void updateTabText(QWidget* page);
    void updateStatus();

    QSplitter* m_splitter = nullptr;
    // The recent projects until a project is opened, then the project tree
    QStackedWidget* m_side = nullptr;
    QListWidget* m_recentList = nullptr;
    QTreeView* m_tree = nullptr;
    QFileSystemModel* m_model = nullptr;
    MruTabWidget* m_tabs = nullptr;
    QMenu* m_recentMenu = nullptr;
    QLabel* m_position = nullptr;
    QString m_projectDirectory;
    QString m_lastFileDirectory;
    RecentProjects m_recent;
    PluginManager m_plugins;
    // The editors of the tabs, by their pages.
    std::unordered_map<QWidget*, std::unique_ptr<Editor>> m_editors;
    bool m_checkingExternalChanges = false;
};

} // namespace eudaimonia
