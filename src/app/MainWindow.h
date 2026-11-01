// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "NavigationHistory.h"
#include "PluginManager.h"
#include "RecentProjects.h"

#include <eudaimonia/plugin/BuildSystem.h>
#include <eudaimonia/plugin/LanguageSupport.h>

#include <QMainWindow>
#include <QPointer>

#include <QHash>
#include <QSet>

#include <memory>
#include <unordered_map>

class MruTabWidget;
class QAction;
class QComboBox;
class QDockWidget;
class QFileSystemModel;
class QProcess;
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

class BuildPanel;
class RunPanel;

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
    struct LanguageListener;

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
    // Saves every tab with unsaved changes; false when one was not saved.
    bool saveAll();

    // Building: the project's build system plugin (CMake), the build
    // directory <project>/build, the executable targets to run.
    void createBuildBar();
    // Finds the build system of the opened project and configures it.
    void setUpBuild();
    // Configures the project in the background ("cmake -S . -B build"):
    // with the chosen build tool (setting "build/backend") when the build
    // directory is new or `switchBackend`, else with the one it has.
    void configure(bool switchBackend = false);
    // Build → Build Tool: the build system's default or one of its tools.
    void fillBackendMenu();
    void chooseBackend(const QString& backend);
    // Reads the targets of the configured project into the target list.
    void loadTargets();
    // Builds the chosen target in the build panel, and with `run` starts the
    // program in a terminal when the build succeeded.
    void buildAndRun(bool run);
    void buildFinished(bool success);
    // Runs the program in the Run panel, or in a terminal (Run in Terminal);
    // a program still running in the panel is stopped first.
    void runProgram(const QString& program, const QString& name);
    void updateBuildActions();
    QString buildDirectory() const;

    // Language servers (the language support plugin): the open project's,
    // told of the documents in the tabs.
    void setUpLanguages();
    // Tells the language servers that the tab shows its file now.
    void languageOpened(Editor& editor);
    void languageClosed(Editor& editor);
    // A file's diagnostics: underlined in its tab and listed in Problems.
    void showDiagnostics(const QString& file, const QList<Diagnostic>& list);
    void updateProblems();
    void goToDefinition();
    // Opens the file at a line and column from 1 (0: the line's start).
    void openLocation(const QString& file, int line, int column);

    // Back and Forward (Ctrl+Alt+Left, Ctrl+Alt+Right) across files.
    NavigationHistory::Place placeOf(QWidget* page) const;
    void navigateBack();
    void navigateForward();
    void goToPlace(const NavigationHistory::Place& place);
    void updateNavigationActions();

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

    BuildSystem* m_buildSystem = nullptr;
    QList<BuildTarget> m_targets;
    QComboBox* m_targetBox = nullptr;
    QAction* m_configureAction = nullptr;
    QAction* m_buildAction = nullptr;
    QAction* m_runAction = nullptr;
    QAction* m_stopAction = nullptr;
    QMenu* m_backendMenu = nullptr;
    QDockWidget* m_buildDock = nullptr;
    BuildPanel* m_buildPanel = nullptr;
    QDockWidget* m_runDock = nullptr;
    RunPanel* m_runPanel = nullptr;
    QAction* m_runInTerminal = nullptr;
    // The program to run once the one running in the panel has stopped.
    QString m_pendingProgram;
    QString m_pendingName;

    LanguageSupport* m_language = nullptr;
    std::unique_ptr<LanguageListener> m_languageListener;
    QHash<QString, QList<Diagnostic>> m_diagnostics;  // by file
    QSet<QString> m_reportedMissingServers;           // told once a session
    QDockWidget* m_problemsDock = nullptr;
    QListWidget* m_problems = nullptr;

    NavigationHistory m_history;
    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QWidget* m_previousTab = nullptr;  // left when the current tab changes
    bool m_navigating = false;         // Back or Forward moving: nothing to record
    // The program to run when the build in progress succeeds; empty for Build.
    QString m_runAfterBuild;
    QString m_runName;
    QPointer<QProcess> m_configuring;
};

} // namespace eudaimonia
