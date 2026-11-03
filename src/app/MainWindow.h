// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "NavigationHistory.h"
#include "PluginManager.h"
#include "RecentProjects.h"

#include <eudaimonia/plugin/BuildSystem.h>
#include <eudaimonia/plugin/Debugger.h>
#include <eudaimonia/plugin/LanguageSupport.h>

#include <QMainWindow>
#include <QPointer>

#include <QHash>
#include <QSet>

#include <functional>
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
class QToolBar;
class QTreeView;

namespace spp {
class EditorPlugin;
class EditorView;
class FindBar;
}

namespace eudaimonia {

class BuildPanel;
class DebugPanel;
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
    struct DebugListener;

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
    // directory of the chosen profile, the executable targets to run.
    void createBuildBar();
    // Another build profile for the project: configures its directory.
    void chooseProfile();
    // Finds the build system of the opened project and configures it.
    void setUpBuild();
    // A configuration in progress whose end is of no interest any more.
    void cancelConfigure();
    // Configures the chosen profile's directory ("cmake -S . -B build") in
    // the background, then reads its targets.
    void configure(bool switchBackend = false);
    // Configures the build directory of a profile, then tells whether it
    // went well.
    void configureDirectory(const QString& profile, bool switchBackend, std::function<void(bool)> then);
    // The command for it: with the chosen build tool (setting "build/backend")
    // when the directory is new or `switchBackend`, else with the one it has;
    // `done` is what to say when it worked.
    BuildCommand configureCommand(const QString& profile, bool switchBackend, QString* done);
    // Build → Build Tool: the build system's default or one of its tools.
    void fillBackendMenu();
    void chooseBackend(const QString& backend);
    // Build → Compile Threads: logical processors, physical cores or a number.
    void fillThreadsMenu();
    // Builds the whole project anew, for its time and memory.
    void rebuild();
    // Options of a build by the chosen threads.
    BuildOptions buildOptions(bool clean = false) const;
    // "12 threads", for the title of a build.
    QString threadsText() const;
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
    // A profile's build directory, as the build system has it (CMake's
    // <project>/build, build-debug and build-release), and how messages name
    // it, e.g. "build/".
    QString directoryOf(const QString& profile) const;
    QString directoryNameOf(const QString& profile) const;
    // Those of the chosen profile.
    QString buildDirectory() const;
    QString buildDirectoryName() const;

    // Debugging (the debugger plugin), with breakpoints kept for each project
    // and shown in the tabs of their files.
    void createDebugging();
    // True, or false after telling the user why it cannot be debugged.
    bool canDebug(DebugRequest::Kind kind);
    // Builds the chosen target in build-debug and debugs the program; that
    // directory whatever the profile, as breakpoints need debug information.
    void debugTarget();
    // Debugs CMake configuring the chosen profile's directory.
    void debugConfigure();
    void startDebugging(const DebugRequest& request, const QString& name);
    void stopDebugging();
    void loadBreakpoints();
    void saveBreakpoints() const;
    // The breakpoints of a file, from the margin of its tab.
    void setFileBreakpoints(const QString& file, const QList<int>& lines);
    // The tab's margin and breakpoints for its file (opened, or renamed).
    void showBreakpoints(Editor& editor);
    void breakpointMoved(const DebugBreakpoint& breakpoint);
    // Shows the file at the line (from 1) as where the debugger stopped.
    void showExecution(const QString& file, int line);
    void clearExecution();

    // Language servers (the language support plugin): the open project's,
    // told of the documents in the tabs.
    void setUpLanguages();
    // The servers of the project and build directory; the documents open
    // already go to them.
    void restartLanguages();
    // Tells the language servers that the tab shows its file now.
    void languageOpened(Editor& editor);
    void languageClosed(Editor& editor);
    // A file's diagnostics: underlined in its tab and listed in Problems.
    void showDiagnostics(const QString& file, const QList<Diagnostic>& list);
    void updateProblems();
    void goToDefinition();
    // Ctrl+Space: what the language server can write at the cursor.
    void completeCode();
    // Asks for the completions, shown in the tab when they come: `asked` by
    // the user says so when there are none.
    void requestCompletions(Editor& editor, bool asked);
    // Selects the word at the cursor (Ctrl+W).
    void selectWord();
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
    spp::FindBar* m_findBar = nullptr;  // under the tabs; null without the find plugin
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
    QComboBox* m_profileBox = nullptr;
    QComboBox* m_targetBox = nullptr;
    QAction* m_configureAction = nullptr;
    QAction* m_buildAction = nullptr;
    QAction* m_runAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_rebuildAction = nullptr;
    QMenu* m_threadsMenu = nullptr;
    QMenu* m_backendMenu = nullptr;
    QDockWidget* m_buildDock = nullptr;
    BuildPanel* m_buildPanel = nullptr;
    QDockWidget* m_runDock = nullptr;
    RunPanel* m_runPanel = nullptr;
    QAction* m_runInTerminal = nullptr;
    // The program to run once the one running in the panel has stopped.
    QString m_pendingProgram;
    QString m_pendingName;

    QToolBar* m_buildBar = nullptr;
    Debugger* m_debugger = nullptr;
    std::unique_ptr<DebugListener> m_debugListener;
    DebugPanel* m_debugPanel = nullptr;
    QDockWidget* m_debugDock = nullptr;
    QAction* m_debugAction = nullptr;
    QAction* m_debugConfigureAction = nullptr;
    QAction* m_resumeAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_stepOverAction = nullptr;
    QAction* m_stepInAction = nullptr;
    QAction* m_stepOutAction = nullptr;
    QAction* m_stopDebugAction = nullptr;
    bool m_debugStopped = false;       // stopped in the debugger: stepping can go on
    bool m_debuggingConfigure = false;
    QString m_debugAfterBuild;         // the program to debug when the build succeeds
    QHash<QString, QList<int>> m_breakpoints;  // lines from 0, by file
    QString m_executionFile;           // where the debugger's line shows

    LanguageSupport* m_language = nullptr;
    std::unique_ptr<LanguageListener> m_languageListener;
    QString m_languageBuildDirectory;  // the one the servers were given
    int m_completionRequest = 0;       // answers to those before are let go
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
