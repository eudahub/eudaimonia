// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"

#include "BuildMonitor.h"
#include "BuildPanel.h"
#include "DebugPanel.h"
#include "RunPanel.h"
#include "Terminal.h"
#include "build_info.h"

#include <mrutabwidget.h>
#include <qxfiledialog.h>
#include <siplasplas/plugin/EditorPlugin.h>
#include <siplasplas/plugin/FindPlugin.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

namespace eudaimonia {
namespace {

// Larger files are not opened: the editor holds a file whole.
constexpr qint64 maxFileSize = 32 * 1024 * 1024;

QString canonicalPath(const QString& path) {
    const QFileInfo info(QxFileDialog::expandHomePath(path));
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

// Files the debuggers can stop in: C and C++ with gdb, CMake with CMake.
bool canHaveBreakpoints(const QString& file) {
    static const QSet<QString> suffixes{QStringLiteral("c"),   QStringLiteral("cc"),  QStringLiteral("cpp"),
                                        QStringLiteral("cxx"), QStringLiteral("c++"), QStringLiteral("h"),
                                        QStringLiteral("hh"),  QStringLiteral("hpp"), QStringLiteral("hxx"),
                                        QStringLiteral("inl"), QStringLiteral("ipp"), QStringLiteral("tpp"),
                                        QStringLiteral("cmake")};
    const QFileInfo info(file);
    return info.fileName() == QLatin1String("CMakeLists.txt") || suffixes.contains(info.suffix().toLower());
}

} // namespace

// What the debugger does.
struct MainWindow::DebugListener : DebuggerListener {
    MainWindow* window = nullptr;
    void stopped(int threadId, const QString& reason) override {
        window->m_debugStopped = true;
        window->m_debugDock->show();
        window->m_debugDock->raise();
        window->updateBuildActions();
        window->m_debugPanel->stopped(threadId, reason);
    }
    void continued() override {
        window->m_debugStopped = false;
        window->clearExecution();
        window->m_debugPanel->continued();
        window->updateBuildActions();
    }
    void output(const QString& text) override { window->m_debugPanel->append(text); }
    void breakpointChanged(const DebugBreakpoint& breakpoint) override { window->breakpointMoved(breakpoint); }
    void ended(int exitCode, const QString& error) override {
        window->m_debugStopped = false;
        window->clearExecution();
        window->m_debugPanel->sessionEnded(exitCode, error);
        // The directory has what CMake configured, when it got that far.
        if (window->m_debuggingConfigure) window->loadTargets();
        window->m_debuggingConfigure = false;
        window->updateBuildActions();
    }
};

// What the language servers report.
struct MainWindow::LanguageListener : LanguageSupportListener {
    MainWindow* window = nullptr;
    void diagnostics(const QString& file, const QList<Diagnostic>& list) override {
        window->showDiagnostics(file, list);
    }
    void message(const QString& text) override { window->statusBar()->showMessage(text, 10000); }
};

// A tab's editor, and what it tells the window.
struct MainWindow::Editor : spp::EditorViewListener {
    MainWindow* window = nullptr;
    QWidget* page = nullptr;
    spp::EditorView* view = nullptr;
    // The file the language servers know the tab by; empty when none does.
    QString languageFile;
    // The file its breakpoints are kept for.
    QString breakpointFile;

    void modificationChanged(bool) override { window->updateTabText(page); }
    void breakpointsChanged(const QList<int>& lines) override { window->setFileBreakpoints(view->filePath(), lines); }
    void completionsNeeded(spp::TextPosition) override { window->requestCompletions(*this, false); }
    void filePathChanged(const QString& path) override {
        window->updateTabText(page);
        window->showBreakpoints(*this);
        // Saved under another name: the servers learn of the new file.
        if (path != languageFile) {
            window->languageClosed(*this);
            window->languageOpened(*this);
        }
    }
    void textChanged() override {
        if (!languageFile.isEmpty() && window->m_language) window->m_language->changed(languageFile, view->text());
        if (window->m_findBar && window->m_tabs->currentWidget() == page) window->m_findBar->editorTextChanged();
    }
    // Ctrl over a word: a link when the language server knows its definition.
    void linkQueried(spp::TextPosition position) override {
        if (languageFile.isEmpty() || !window->m_language) return;  // no server: no links
        QPointer<QWidget> asker = page;
        MainWindow* owner = window;
        window->m_language->findDefinition(
            languageFile, {position.line, position.column}, [owner, asker, position](const QList<SourceLocation>& found) {
                // The tab may be gone by the time the server answers.
                if (!asker) return;
                if (spp::EditorView* view = owner->viewOf(asker)) view->answerLink(position, !found.isEmpty());
            });
    }
    // Ctrl+click: the definition of what was clicked (the cursor is there).
    void linkActivated(spp::TextPosition) override {
        if (window->m_tabs->currentWidget() == page) window->goToDefinition();
    }
    void cursorMoved(spp::TextPosition position) override {
        // A long move within the file (a click far away, Ctrl+End) is a
        // place to come back to, as a jump to another file is.
        if (!window->m_navigating && std::abs(position.line - line) >= 10) {
            window->m_history.leave({view->filePath(), line, column});
            window->updateNavigationActions();
        }
        line = position.line;
        column = position.column;
        if (window->m_tabs->currentWidget() == page) window->updateStatus();
    }
    int line = 0;    // the cursor's, as last told
    int column = 0;
    void formatChanged() override {
        if (window->m_tabs->currentWidget() == page) window->updateStatus();
    }
};

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_model = new QFileSystemModel(this);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    m_model->setReadOnly(true);

    m_tree = new QTreeView;
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    for (int column = 1; column < m_model->columnCount(); ++column) m_tree->hideColumn(column);
    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex& index) {
        if (!m_model->isDir(index)) openFile(m_model->filePath(index));
    });

    m_recentList = new QListWidget;
    m_recentList->setTextElideMode(Qt::ElideMiddle);
    connect(m_recentList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        openProject(item->data(Qt::UserRole).toString());
    });
    auto* openButton = new QPushButton(tr("Open Project Directory..."));
    connect(openButton, &QPushButton::clicked, this, &MainWindow::chooseProject);
    auto* start = new QWidget;
    auto* startLayout = new QVBoxLayout(start);
    startLayout->setContentsMargins(0, 0, 0, 0);
    startLayout->addWidget(new QLabel(tr("Recent projects")));
    startLayout->addWidget(m_recentList, 1);
    startLayout->addWidget(openButton);

    m_side = new QStackedWidget;
    m_side->addWidget(start);
    m_side->addWidget(m_tree);

    m_tabs = new MruTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(true);
    m_tabs->setSharedTabWidths(true);
    m_tabs->setTabsClosable(true);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        // Leaving a tab is leaving a place (also for a file opened or a jump).
        if (!m_navigating && m_previousTab && m_previousTab != m_tabs->currentWidget()
            && m_editors.count(m_previousTab)) {
            m_history.leave(placeOf(m_previousTab));
            updateNavigationActions();
        }
        m_previousTab = m_tabs->currentWidget();
        if (m_findBar) m_findBar->setEditor(viewOf(m_tabs->currentWidget()));
        updateTitle();
        updateStatus();
    });
    connect(m_tabs, &MruTabWidget::tabAboutToClose, this, [this](QWidget* page, bool, bool& allow) {
        if (allow) allow = confirmClose(page);
    });
    connect(m_tabs, &MruTabWidget::tabClosed, this, [this](QWidget* page) {
        const auto editor = m_editors.find(page);
        if (editor == m_editors.end()) return;
        languageClosed(*editor->second);
        editor->second->view->setListener(nullptr);
        m_editors.erase(editor);
        if (m_previousTab == page) m_previousTab = nullptr;
    });

    m_position = new QLabel;
    statusBar()->addPermanentWidget(m_position);

    // The tabs, and under them the find bar (the find plugin's) working on
    // the current tab's editor.
    auto* editors = new QWidget;
    auto* editorsLayout = new QVBoxLayout(editors);
    editorsLayout->setContentsMargins(0, 0, 0, 0);
    editorsLayout->setSpacing(0);
    editorsLayout->addWidget(m_tabs, 1);
    if (auto* find = m_plugins.plugin<spp::FindPlugin>()) {
        m_findBar = find->createFindBar(editors);
        editorsLayout->addWidget(m_findBar->widget());
    }

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(m_side);
    m_splitter->addWidget(editors);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 900});
    setCentralWidget(m_splitter);

    createMenus();
    createBuildBar();
    createDebugging();
    setUpLanguages();
    updateRecentProjects();
    updateTitle();
    resize(1200, 800);
    restoreState();
}

// The pages may outlive this object's own part: their editors tell no one.
MainWindow::~MainWindow() {
    cancelConfigure();
    // The debugger and the program stop with the window.
    if (m_debugger) {
        m_debugger->setListener(nullptr);
        m_debugger->stop();
    }
    if (m_findBar) m_findBar->setEditor(nullptr);
    for (const auto& [page, editor] : m_editors) editor->view->setListener(nullptr);
    // The language servers stop with the window.
    if (m_language) {
        m_language->setListener(nullptr);
        m_language->shutdown();
    }
}

void MainWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* openProject = file->addAction(tr("Open &Project Directory..."), this, &MainWindow::chooseProject);
    openProject->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    QAction* openFile = file->addAction(tr("&Open File..."), this, &MainWindow::chooseFile);
    openFile->setShortcut(QKeySequence::Open);
    m_recentMenu = file->addMenu(tr("&Recent Projects"));
    file->addSeparator();
    QAction* save = file->addAction(tr("&Save"), this, [this] {
        if (QWidget* page = m_tabs->currentWidget()) saveTab(page);
    });
    save->setShortcut(QKeySequence::Save);
    QAction* saveAs = file->addAction(tr("Save &As..."), this, [this] {
        if (QWidget* page = m_tabs->currentWidget()) saveTab(page, true);
    });
    saveAs->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    QAction* saveAll = file->addAction(tr("Save A&ll"), this, &MainWindow::saveAll);
    saveAll->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    QAction* closeTab = file->addAction(tr("&Close Tab"), this, [this] {
        if (m_tabs->currentWidget()) m_tabs->requestCloseTab(m_tabs->currentWidget());
    });
    closeTab->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F4));
    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    // Finding and replacing: the find bar under the tabs.
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("Select &Word"), this, &MainWindow::selectWord)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    edit->addAction(tr("&Complete Code"), this, &MainWindow::completeCode)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
    edit->addSeparator();
    auto withBar = [this](void (spp::FindBar::*act)()) {
        return [this, act] {
            if (!m_findBar) {
                QMessageBox::critical(this, tr("Find Plugin Missing"),
                                      m_plugins.missingMessage(QStringLiteral(SPP_FIND_PLUGIN_IID), tr("Finding")));
                return;
            }
            if (m_tabs->currentWidget()) (m_findBar->*act)();
        };
    };
    edit->addAction(tr("&Find..."), this, withBar(&spp::FindBar::showFind))->setShortcut(QKeySequence::Find);
    edit->addAction(tr("&Replace..."), this, withBar(&spp::FindBar::showReplace))
        ->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    edit->addAction(tr("Find &Next"), this, withBar(&spp::FindBar::findNext))->setShortcut(QKeySequence(Qt::Key_F3));
    edit->addAction(tr("Find &Previous"), this, withBar(&spp::FindBar::findPrevious))
        ->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&Plugins..."), this, &MainWindow::showPlugins);
    help->addAction(tr("&About Eudaimonia"), this, [this] {
        // What was built, so that it is plain which versions are in use.
        struct Component {
            const char* name;
            const char* tag;
            const char* commit;
            const char* time;
        };
        static const Component components[] = {EUDAIMONIA_COMPONENTS};
        QStringList built;
        for (const Component& component : components) {
            QString line = QStringLiteral("%1 %2").arg(QString::fromUtf8(component.name),
                                                       QString::fromUtf8(component.tag));
            if (*component.commit)
                line += QStringLiteral(" (%1, %2)").arg(QString::fromUtf8(component.commit),
                                                        QString::fromUtf8(component.time));
            built << line.toHtmlEscaped();
        }
        QMessageBox::about(this, tr("About Eudaimonia"),
                           tr("<b>Eudaimonia</b> %1<br>Commit %2, %3<br>An integrated development "
                              "environment.</p><p>Built with:<br>%4<br>Qt %5 (running %6)</p>"
                              "<p>Licensed under the GNU General Public License, version 3 only.")
                               .arg(QStringLiteral(EUDAIMONIA_VERSION), QStringLiteral(EUDAIMONIA_COMMIT),
                                    QStringLiteral(EUDAIMONIA_COMMIT_TIME), built.join(QStringLiteral("<br>")),
                                    QStringLiteral(QT_VERSION_STR), QString::fromLatin1(qVersion())));
    });
}

bool MainWindow::openProject(const QString& directory) {
    const QString path = canonicalPath(directory);
    if (!QFileInfo(path).isDir()) {
        QMessageBox::warning(this, tr("Open Project"), tr("%1 is not a directory.").arg(path));
        return false;
    }
    m_projectDirectory = path;
    m_model->setRootPath(path);
    m_tree->setRootIndex(m_model->index(path));
    m_side->setCurrentWidget(m_tree);
    QString error;
    if (!m_recent.remember(path, &error))
        statusBar()->showMessage(tr("Cannot save the recent projects: %1").arg(error), 5000);
    updateRecentProjects();
    updateTitle();
    stopDebugging();
    loadBreakpoints();
    setUpBuild();
    if (m_language) {
        restartLanguages();
        // The servers the project needs and the computer lacks, told once a
        // session each: another project may need other ones.
        QStringList missing;
        for (const LanguageServerInfo& server : m_language->serversFor(path)) {
            if (server.installed || m_reportedMissingServers.contains(server.name)) continue;
            m_reportedMissingServers.insert(server.name);
            missing << tr("<li><b>%1</b> for %2 — install it with <code>%3</code></li>")
                           .arg(server.name.toHtmlEscaped(), server.languages.toHtmlEscaped(),
                                server.installHint.toHtmlEscaped());
        }
        if (!missing.isEmpty())
            QMessageBox::information(
                this, tr("Language Servers Missing"),
                tr("<p>%1 has files whose language servers are not installed:</p><ul>%2</ul>"
                   "<p>Without them those files get no diagnostics and no Go to Definition. "
                   "Reopen the project after installing.</p>")
                    .arg(QFileInfo(path).fileName().toHtmlEscaped(), missing.join(QString())));
    }
    return true;
}

bool MainWindow::openFile(const QString& filePath) {
    const QString path = canonicalPath(filePath);
    if (QWidget* page = m_tabs->findTab(path)) {
        m_tabs->setCurrentWidget(page);
        return true;
    }
    if (QFileInfo(path).size() > maxFileSize) {
        QMessageBox::warning(this, tr("Open File"),
                             tr("%1 is larger than %2 MiB.").arg(path).arg(maxFileSize / (1024 * 1024)));
        return false;
    }
    spp::EditorPlugin* plugin = editorPlugin();
    if (!plugin) return false;
    spp::EditorView* view = plugin->createEditor();
    std::unique_ptr<QWidget> page(view->widget());
    const spp::EditorResult result = view->open(path);
    if (result != spp::EditorResult::Ok) {
        if (result == spp::EditorResult::Failed)
            QMessageBox::warning(this, tr("Open File"), tr("Cannot open %1: %2").arg(path, view->errorString()));
        return false;
    }
    if (!m_tabs->makeRoomForNewTab()) return false;

    auto editor = std::make_unique<Editor>();
    editor->window = this;
    editor->page = page.get();
    editor->view = view;
    view->setListener(editor.get());
    Editor& opened = *editor;
    m_editors.emplace(page.get(), std::move(editor));
    showBreakpoints(opened);
    languageOpened(opened);
    QWidget* added = page.release();
    const int index = m_tabs->addTab(added, QFileInfo(path).fileName());
    updateTabText(added);
    m_tabs->setCurrentIndex(index);
    added->setFocus();
    m_lastFileDirectory = QFileInfo(path).absolutePath();
    const QStringList uncertain = view->uncertainEncodings();
    if (!uncertain.isEmpty())
        statusBar()->showMessage(tr("Encoding of %1 uncertain (%2); shown as %3")
                                     .arg(QFileInfo(path).fileName(), uncertain.join(QStringLiteral(", ")),
                                          view->encoding()),
                                 10000);
    return true;
}

spp::EditorPlugin* MainWindow::editorPlugin() {
    if (auto* plugin = m_plugins.plugin<spp::EditorPlugin>()) return plugin;
    QMessageBox::critical(this, tr("Editor Plugin Missing"),
                          m_plugins.missingMessage(QStringLiteral(SPP_EDITOR_PLUGIN_IID), tr("Opening files")));
    return nullptr;
}

spp::EditorView* MainWindow::viewOf(QWidget* page) const {
    const auto editor = m_editors.find(page);
    return editor == m_editors.end() ? nullptr : editor->second->view;
}

bool MainWindow::saveTab(QWidget* page, bool askName) {
    spp::EditorView* view = viewOf(page);
    if (!view) return false;
    QString path = view->filePath();
    if (askName || path.isEmpty()) {
        const QString start = path.isEmpty() ? m_projectDirectory : path;
        path = QxFileDialog::getSaveFileName(this, tr("Save As"), QFileInfo(start).absolutePath(), {},
                                             QFileInfo(path).fileName());
        if (path.isEmpty()) return false;
    }
    const spp::EditorResult result = path == view->filePath() ? view->save() : view->saveAs(path);
    if (result == spp::EditorResult::Failed)
        QMessageBox::warning(this, tr("Save"), tr("Cannot save %1: %2").arg(path, view->errorString()));
    if (result != spp::EditorResult::Ok) return false;
    statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
    const auto editor = m_editors.find(page);
    if (m_language && editor != m_editors.end() && !editor->second->languageFile.isEmpty())
        m_language->saved(editor->second->languageFile);
    return true;
}

bool MainWindow::confirmClose(QWidget* page) {
    spp::EditorView* view = viewOf(page);
    if (!view || !view->isModified()) return true;
    m_tabs->setCurrentWidget(page);
    const auto answer = QMessageBox::question(
        this, tr("Unsaved Changes"), tr("%1 has unsaved changes. Save them?").arg(QFileInfo(view->filePath()).fileName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) return saveTab(page);
    return answer == QMessageBox::Discard;
}

void MainWindow::checkExternalChanges() {
    if (m_checkingExternalChanges) return;
    m_checkingExternalChanges = true;
    for (const auto& [page, editor] : m_editors) {
        spp::EditorView* view = editor->view;
        if (!view->hasExternalChanges()) continue;
        const QString name = QFileInfo(view->filePath()).fileName();
        // Reloading is an undo step: Ctrl+Z brings the text before back.
        if (view->isModified()
            && QMessageBox::question(this, tr("File Changed on Disk"),
                                     tr("%1 changed on disk and has unsaved changes here. Load the disk version? "
                                        "Undo brings your changes back.")
                                         .arg(name))
                   != QMessageBox::Yes) {
            view->ignoreExternalChanges();
            continue;
        }
        if (view->reload() == spp::EditorResult::Ok)
            statusBar()->showMessage(tr("Reloaded %1, changed on disk").arg(name), 5000);
        else
            view->ignoreExternalChanges();
    }
    m_checkingExternalChanges = false;
}

void MainWindow::updateTabText(QWidget* page) {
    spp::EditorView* view = viewOf(page);
    const int index = m_tabs->indexOf(page);
    if (!view || index < 0) return;
    const QString path = view->filePath();
    m_tabs->setTabKey(page, path);
    m_tabs->setTabText(index, QFileInfo(path).fileName() + (view->isModified() ? QStringLiteral(" *") : QString()));
    m_tabs->setTabToolTip(index, path);
    const QString shown = !m_projectDirectory.isEmpty() && path.startsWith(m_projectDirectory + '/')
                              ? QDir(m_projectDirectory).relativeFilePath(path)
                              : path;
    m_tabs->setTabPopupText(index, shown);
    if (page == m_tabs->currentWidget()) updateTitle();
}

void MainWindow::updateStatus() {
    spp::EditorView* view = viewOf(m_tabs->currentWidget());
    if (!view) {
        m_position->clear();
        return;
    }
    const spp::TextPosition position = view->cursorPosition();
    m_position->setText(tr("Line %1, Column %2   %3").arg(position.line + 1).arg(position.column + 1).arg(view->encoding()));
}

bool MainWindow::saveAll() {
    for (const auto& [page, editor] : m_editors)
        if (editor->view->isModified() && !saveTab(page)) return false;
    return true;
}

namespace {

// The setting naming the target run in a project.
QString runTargetKey(const QString& project) {
    return QStringLiteral("runTarget/") + QString::fromLatin1(project.toUtf8().toHex());
}

// The setting naming the build profile of a project, by its directory.
QString profileKey(const QString& project) {
    return QStringLiteral("buildProfile/") + QString::fromLatin1(project.toUtf8().toHex());
}

// The build type of a profile's directory; `build` leaves it to the project.
QString buildTypeOf(const QString& directory) {
    if (directory == QLatin1String("build-debug")) return QStringLiteral("Debug");
    if (directory == QLatin1String("build-release")) return QStringLiteral("Release");
    return {};
}

} // namespace

void MainWindow::createBuildBar() {
    QToolBar* bar = addToolBar(tr("Build"));
    m_buildBar = bar;
    bar->setObjectName(QStringLiteral("buildBar"));
    m_profileBox = new QComboBox;
    m_profileBox->setObjectName(QStringLiteral("buildProfile"));
    m_profileBox->addItem(tr("Default"), QStringLiteral("build"));
    m_profileBox->addItem(tr("Debug"), QStringLiteral("build-debug"));
    m_profileBox->addItem(tr("Release"), QStringLiteral("build-release"));
    m_profileBox->setToolTip(tr("The build profile, each with its build directory"));
    connect(m_profileBox, &QComboBox::activated, this, &MainWindow::chooseProfile);
    bar->addWidget(m_profileBox);
    m_targetBox = new QComboBox;
    m_targetBox->setMinimumContentsLength(16);
    m_targetBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_targetBox->setToolTip(tr("The program Run starts"));
    connect(m_targetBox, &QComboBox::activated, this, [this] {
        QSettings().setValue(runTargetKey(m_projectDirectory), m_targetBox->currentText());
    });
    bar->addWidget(m_targetBox);

    QMenu* build = menuBar()->insertMenu(menuBar()->actions().last(), new QMenu(tr("&Build"), this))->menu();
    m_buildAction = build->addAction(QIcon(QStringLiteral(":/icons/hammer.svg")), tr("&Build"), this,
                                     [this] { buildAndRun(false); });
    m_buildAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F9));
    m_buildAction->setToolTip(tr("Build the chosen target (Ctrl+F9)"));
    m_runAction = build->addAction(QIcon(QStringLiteral(":/icons/run.svg")), tr("&Run"), this,
                                   [this] { buildAndRun(true); });
    m_runAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F10));
    m_runAction->setToolTip(tr("Build the chosen target and run it (Shift+F10)"));
    m_rebuildAction = build->addAction(tr("Re&build Project"), this, &MainWindow::rebuild);
    m_rebuildAction->setToolTip(tr("Build the whole project anew, to see its time and memory"));
    m_stopAction = build->addAction(tr("&Stop"), this, [this] {
        if (m_buildPanel->isRunning()) m_buildPanel->stop();
        else if (m_debugger && m_debugger->isRunning()) m_debugger->stop();
        else m_runPanel->stop();
    });
    m_stopAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F2));
    m_stopAction->setToolTip(tr("Stop the build, the debugging, or the program running (Ctrl+F2)"));
    m_runInTerminal = build->addAction(tr("Run in &Terminal"));
    m_runInTerminal->setCheckable(true);
    m_runInTerminal->setChecked(QSettings().value(QStringLiteral("run/inTerminal"), false).toBool());
    m_runInTerminal->setToolTip(tr("Run programs in a terminal window, e.g. full-screen ones"));
    connect(m_runInTerminal, &QAction::toggled, this,
            [](bool checked) { QSettings().setValue(QStringLiteral("run/inTerminal"), checked); });
    build->addSeparator();
    m_configureAction = build->addAction(tr("&Configure Project"), this, [this] { configure(); });
    m_threadsMenu = build->addMenu(tr("Compile T&hreads"));
    connect(m_threadsMenu, &QMenu::aboutToShow, this, &MainWindow::fillThreadsMenu);
    m_backendMenu = build->addMenu(tr("Build &Tool"));
    connect(m_backendMenu, &QMenu::aboutToShow, this, &MainWindow::fillBackendMenu);
    bar->addAction(m_buildAction);
    bar->addAction(m_runAction);
    bar->addAction(m_stopAction);

    // The build's output, time and memory, below the editors.
    m_buildPanel = new BuildPanel;
    connect(m_buildPanel, &BuildPanel::started, this, &MainWindow::updateBuildActions);
    connect(m_buildPanel, &BuildPanel::finished, this, &MainWindow::buildFinished);
    // A compiler's message clicked: its file at its line and column.
    connect(m_buildPanel, &BuildPanel::locationActivated, this, &MainWindow::openLocation);
    m_buildDock = new QDockWidget(tr("Build"), this);
    m_buildDock->setObjectName(QStringLiteral("buildDock"));
    m_buildDock->setWidget(m_buildPanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_buildDock);
    m_buildDock->hide();
    build->addAction(m_buildDock->toggleViewAction());

    // The program's output and input, beside the build's.
    m_runPanel = new RunPanel;
    connect(m_runPanel, &RunPanel::started, this, &MainWindow::updateBuildActions);
    connect(m_runPanel, &RunPanel::finished, this, [this] {
        updateBuildActions();
        if (m_pendingProgram.isEmpty()) return;
        const QString program = m_pendingProgram;
        m_pendingProgram.clear();
        runProgram(program, m_pendingName);
    });
    m_runDock = new QDockWidget(tr("Run"), this);
    m_runDock->setObjectName(QStringLiteral("runDock"));
    m_runDock->setWidget(m_runPanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_runDock);
    tabifyDockWidget(m_buildDock, m_runDock);
    m_runDock->hide();
    build->addAction(m_runDock->toggleViewAction());
    updateBuildActions();
}

QString MainWindow::buildDirectory() const {
    return QDir(m_projectDirectory).filePath(m_profileBox->currentData().toString());
}

QString MainWindow::buildDirectoryName() const {
    return m_profileBox->currentData().toString() + QLatin1Char('/');
}

void MainWindow::chooseProfile() {
    QSettings().setValue(profileKey(m_projectDirectory), m_profileBox->currentData());
    m_targets.clear();
    m_targetBox->clear();
    configure();
}

void MainWindow::cancelConfigure() {
    if (!m_configuring) return;
    disconnect(m_configuring, nullptr, this, nullptr);
    m_configuring->kill();
    m_configuring->deleteLater();
    m_configuring = nullptr;
}

void MainWindow::setUpBuild() {
    cancelConfigure();
    m_buildSystem = nullptr;
    m_targets.clear();
    m_targetBox->clear();
    const int profile = m_profileBox->findData(QSettings().value(profileKey(m_projectDirectory)));
    m_profileBox->setCurrentIndex(std::max(0, profile));
    // A project the build system plugin does not know has nothing to build;
    // without the plugin Build and Run say so when used.
    if (auto* buildSystem = m_plugins.plugin<BuildSystem>()) {
        if (buildSystem->recognizes(m_projectDirectory)) {
            m_buildSystem = buildSystem;
            configure();
        }
    } else if (QFileInfo(QDir(m_projectDirectory).filePath(QStringLiteral("CMakeLists.txt"))).isFile()) {
        statusBar()->showMessage(tr("No build system plugin: the project cannot be built here"), 10000);
    }
    updateBuildActions();
}

namespace {

// The build tool for new build directories: the one chosen (empty: the build
// system's own default), else the build system's preferred (for CMake Ninja,
// when installed).
QString chosenBackend(const BuildSystem* buildSystem) {
    const QSettings settings;
    if (settings.contains(QStringLiteral("build/backend"))) return settings.value(QStringLiteral("build/backend")).toString();
    const QStringList backends = buildSystem ? buildSystem->backends() : QStringList();
    return backends.isEmpty() ? QString() : backends.first();
}

// At most this many links (memory hungry) at once while compiling uses every
// job; 2 by default, 0: no limit.
int linkJobs() {
    return QSettings().value(QStringLiteral("build/linkJobs"), 2).toInt();
}

} // namespace

void MainWindow::fillBackendMenu() {
    m_backendMenu->clear();
    auto* group = new QActionGroup(m_backendMenu);
    BuildSystem* buildSystem = m_buildSystem ? m_buildSystem : m_plugins.plugin<BuildSystem>();
    const QString chosen = chosenBackend(buildSystem);
    auto add = [&](const QString& text, const QString& backend) {
        QAction* action = m_backendMenu->addAction(text, this, [this, backend] { chooseBackend(backend); });
        action->setCheckable(true);
        action->setChecked(backend == chosen);
        group->addAction(action);
    };
    add(buildSystem ? tr("%1's Own Default").arg(buildSystem->name()) : tr("Default"), QString());
    if (buildSystem)
        for (const QString& backend : buildSystem->backends()) add(backend, backend);
    // Compiling uses every processor; links, which take much memory, fewer.
    m_backendMenu->addSeparator();
    m_backendMenu->addAction(tr("Links at once (Ninja):"))->setEnabled(false);
    auto* linkGroup = new QActionGroup(m_backendMenu);
    for (const int jobs : {1, 2, 4, 0}) {
        QAction* action = m_backendMenu->addAction(jobs ? QString::number(jobs) : tr("As many as compiling"), this,
                                                   [this, jobs] {
            QSettings().setValue(QStringLiteral("build/linkJobs"), jobs);
            // The build directory learns it by configuring.
            if (m_buildSystem && m_buildSystem->supportsLinkJobs(m_buildSystem->configuredBackend(buildDirectory())))
                configure();
        });
        action->setCheckable(true);
        action->setChecked(jobs == linkJobs());
        linkGroup->addAction(action);
    }
    if (m_buildSystem) {
        const QString configured = m_buildSystem->configuredBackend(buildDirectory());
        m_backendMenu->addSeparator();
        QString state = configured.isEmpty() ? tr("%1 is not configured yet").arg(buildDirectoryName())
                                             : tr("%1 uses %2").arg(buildDirectoryName(), configured);
        if (!configured.isEmpty() && linkJobs() > 0 && !m_buildSystem->supportsLinkJobs(configured))
            state += tr(", which links as many at once as it compiles");
        m_backendMenu->addAction(state)->setEnabled(false);
    }
}

namespace {

// The setting of the compile threads: 0 logical processors, the default;
// -1 physical cores; else their number.
int chosenThreads() {
    return QSettings().value(QStringLiteral("build/threads"), 0).toInt();
}

} // namespace

BuildOptions MainWindow::buildOptions(bool clean) const {
    BuildOptions options;
    const int chosen = chosenThreads();
    options.jobs = chosen < 0 ? BuildMonitor::physicalCores() : chosen;
    options.clean = clean;
    return options;
}

QString MainWindow::threadsText() const {
    int threads = buildOptions().jobs;
    // Logical processors unless the environment decides; the build system says.
    if (threads == 0) {
        const int level = qEnvironmentVariableIntValue("CMAKE_BUILD_PARALLEL_LEVEL");
        threads = level > 0 ? level : QThread::idealThreadCount();
    }
    return threads == 1 ? tr("1 thread") : tr("%1 threads").arg(threads);
}

void MainWindow::fillThreadsMenu() {
    m_threadsMenu->clear();
    auto* group = new QActionGroup(m_threadsMenu);
    const int chosen = chosenThreads();
    auto choose = [](int threads) { QSettings().setValue(QStringLiteral("build/threads"), threads); };
    auto add = [&](const QString& text, int threads) {
        QAction* action = m_threadsMenu->addAction(text, this, [choose, threads] { choose(threads); });
        action->setCheckable(true);
        action->setChecked(threads == chosen);
        group->addAction(action);
    };
    add(tr("Logical Processors (%1)").arg(QThread::idealThreadCount()), 0);
    add(tr("Physical Cores (%1)").arg(BuildMonitor::physicalCores()), -1);
    add(QStringLiteral("1"), 1);
    // A number chosen before stays on the list.
    if (chosen > 1) add(QString::number(chosen), chosen);
    m_threadsMenu->addSeparator();
    m_threadsMenu->addAction(tr("Other Number..."), this, [this, chosen, choose] {
        bool accepted = false;
        const int threads = QInputDialog::getInt(this, tr("Compile Threads"), tr("Compile at once:"),
                                                 chosen > 0 ? chosen : QThread::idealThreadCount(), 1, 4096, 1, &accepted);
        if (accepted) choose(threads);
    });
}

void MainWindow::rebuild() {
    if (!m_buildSystem || m_buildPanel->isRunning() || !saveAll()) return;
    m_runAfterBuild.clear();
    m_debugAfterBuild.clear();
    m_buildDock->show();
    m_buildDock->raise();
    m_buildPanel->start(m_buildSystem->buildCommand(buildDirectory(), {}, buildOptions(true)),
                        tr("Rebuild %1 in %2, %3")
                            .arg(QFileInfo(m_projectDirectory).fileName(), buildDirectoryName(), threadsText()));
}

void MainWindow::chooseBackend(const QString& backend) {
    QSettings().setValue(QStringLiteral("build/backend"), backend);
    if (!m_buildSystem || backend.isEmpty() || m_configuring) return;
    const QString configured = m_buildSystem->configuredBackend(buildDirectory());
    if (configured.isEmpty() || configured == backend) return;
    // Switching rebuilds everything: the open project switches when asked.
    if (QMessageBox::question(this, tr("Build Tool"),
                              tr("%1 of %2 uses %3. Configure it afresh for %4? Everything is built again.")
                                  .arg(buildDirectoryName(), QFileInfo(m_projectDirectory).fileName(), configured, backend))
        == QMessageBox::Yes)
        configure(true);
}

void MainWindow::configure(bool switchBackend) {
    configureDirectory(m_profileBox->currentData().toString(), switchBackend, [this](bool) {
        // Another profile: its compile_commands.json exists now.
        if (m_language && m_languageBuildDirectory != buildDirectory()) restartLanguages();
        loadTargets();
    });
}

BuildCommand MainWindow::configureCommand(const QString& directoryName, bool switchBackend, QString* done) {
    const QString directory = QDir(m_projectDirectory).filePath(directoryName);
    // A configured build directory keeps its build tool unless asked to switch.
    const QString configured = m_buildSystem->configuredBackend(directory);
    const QString chosen = chosenBackend(m_buildSystem);
    *done = tr("Configured %1").arg(QFileInfo(m_projectDirectory).fileName());
    if (!configured.isEmpty() && !chosen.isEmpty() && configured != chosen && !switchBackend)
        *done = tr("%1/ uses %2, not %3: Build → Build Tool switches it").arg(directoryName, configured, chosen);
    ConfigureOptions options;
    options.backend = configured.isEmpty() || switchBackend ? chosen : QString();
    options.linkJobs = linkJobs();
    options.buildType = buildTypeOf(directoryName);
    return m_buildSystem->configureCommand(m_projectDirectory, directory, options);
}

void MainWindow::configureDirectory(const QString& directoryName, bool switchBackend, std::function<void(bool)> then) {
    if (!m_buildSystem || m_configuring) return;
    QString done;
    const BuildCommand command = configureCommand(directoryName, switchBackend, &done);
    auto* process = new QProcess(this);
    m_configuring = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(command.workingDirectory);
    const QString project = m_projectDirectory;
    statusBar()->showMessage(tr("Configuring %1 with %2...").arg(QFileInfo(project).fileName(), m_buildSystem->name()));
    connect(process, &QProcess::finished, this, [this, process, project, done, then](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        m_configuring = nullptr;
        if (project != m_projectDirectory) return;  // another project meanwhile
        updateBuildActions();
        if (status != QProcess::NormalExit || exitCode != 0) {
            statusBar()->clearMessage();
            QMessageBox box(QMessageBox::Warning, tr("Configure"),
                            tr("Configuring %1 failed.").arg(QFileInfo(project).fileName()), QMessageBox::Ok, this);
            box.setDetailedText(QString::fromLocal8Bit(process->readAll()));
            box.exec();
            then(false);
            return;
        }
        statusBar()->showMessage(done, 10000);
        then(true);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        statusBar()->showMessage(tr("Cannot start %1: %2").arg(process->program(), process->errorString()), 10000);
        process->deleteLater();
        m_configuring = nullptr;
        updateBuildActions();
    });
    process->start(command.program, command.arguments);
    updateBuildActions();
}

void MainWindow::loadTargets() {
    if (!m_buildSystem) return;
    QString error;
    const QList<BuildTarget> targets = m_buildSystem->targets(buildDirectory(), &error);
    QList<BuildTarget> executables;
    for (const BuildTarget& target : targets)
        if (!target.executable.isEmpty()) executables << target;
    const QString current = m_targetBox->currentText();
    m_targets = executables;
    m_targetBox->clear();
    for (const BuildTarget& target : m_targets) m_targetBox->addItem(target.name);
    // The target chosen before in this project, or the only one.
    const QString remembered = QSettings().value(runTargetKey(m_projectDirectory), current).toString();
    const int index = m_targetBox->findText(remembered);
    m_targetBox->setCurrentIndex(index >= 0 ? index : 0);
    if (!error.isEmpty()) statusBar()->showMessage(error, 5000);
    updateBuildActions();
}

void MainWindow::updateBuildActions() {
    const bool building = m_buildPanel && m_buildPanel->isRunning();
    const bool running = m_runPanel && m_runPanel->isRunning();
    const bool debugging = m_debugger && m_debugger->isRunning();
    if (m_stopAction) m_stopAction->setEnabled(building || running || debugging);
    const bool idle = m_buildSystem && !m_configuring && !building;
    // CMake being debugged is configuring.
    m_profileBox->setEnabled(idle && !debugging);
    m_configureAction->setEnabled(idle && !debugging);
    m_buildAction->setEnabled(idle);
    m_rebuildAction->setEnabled(idle);
    m_runAction->setEnabled(idle && m_targetBox->count() > 0);
    m_targetBox->setEnabled(m_targetBox->count() > 0);
    if (!m_debugAction) return;  // the debugging's actions come later
    // Without the plugin they say so when used.
    m_debugAction->setEnabled(idle && !debugging && m_targetBox->count() > 0);
    m_debugConfigureAction->setEnabled(idle && !debugging);
    m_resumeAction->setEnabled(debugging && m_debugStopped);
    m_stepOverAction->setEnabled(debugging && m_debugStopped);
    m_stepInAction->setEnabled(debugging && m_debugStopped);
    m_stepOutAction->setEnabled(debugging && m_debugStopped);
    m_pauseAction->setEnabled(debugging && !m_debugStopped);
    m_stopDebugAction->setEnabled(debugging);
}

void MainWindow::buildAndRun(bool run) {
    if (!m_buildSystem) {
        if (!m_plugins.plugin<BuildSystem>())
            QMessageBox::critical(this, tr("Build System Plugin Missing"),
                                  m_plugins.missingMessage(QStringLiteral(EUDAIMONIA_BUILDSYSTEM_IID), tr("Building")));
        return;
    }
    if (m_buildPanel->isRunning() || !saveAll()) return;
    const int index = m_targetBox->currentIndex();
    const BuildTarget* target = index >= 0 && index < m_targets.size() ? &m_targets[index] : nullptr;
    if (run && !target) return;
    // Without a chosen target Build builds everything.
    const BuildCommand build = m_buildSystem->buildCommand(buildDirectory(), target ? target->name : QString(), buildOptions());
    m_runAfterBuild = run ? target->executable : QString();
    m_runName = run ? target->name : QString();
    m_buildDock->show();
    m_buildPanel->start(build, tr("Build %1, %2").arg(target ? target->name : QFileInfo(m_projectDirectory).fileName(), threadsText()));
}

void MainWindow::buildFinished(bool success) {
    updateBuildActions();
    // A build reconfigures by itself after CMakeLists.txt changed.
    loadTargets();
    const QString program = m_runAfterBuild;
    m_runAfterBuild.clear();
    const QString debugged = m_debugAfterBuild;
    m_debugAfterBuild.clear();
    if (!success) return;
    if (!debugged.isEmpty()) {
        // In its build directory, as Run runs it.
        startDebugging({DebugRequest::Kind::Program, debugged, {}, QFileInfo(debugged).absolutePath()}, m_runName);
        return;
    }
    if (program.isEmpty()) return;
    runProgram(program, m_runName);
}

void MainWindow::createDebugging() {
    m_debugger = m_plugins.plugin<Debugger>();
    m_debugPanel = new DebugPanel;
    m_debugPanel->setDebugger(m_debugger);
    connect(m_debugPanel, &DebugPanel::frameChosen, this, &MainWindow::showExecution);
    if (m_debugger) {
        m_debugListener = std::make_unique<DebugListener>();
        m_debugListener->window = this;
        m_debugger->setListener(m_debugListener.get());
    }

    QMenu* debug = menuBar()->insertMenu(menuBar()->actions().last(), new QMenu(tr("&Debug"), this))->menu();
    auto running = [this](void (Debugger::*act)()) {
        return [this, act] {
            if (m_debugger && m_debugger->isRunning()) (m_debugger->*act)();
        };
    };
    m_debugAction = debug->addAction(QIcon(QStringLiteral(":/icons/ladybug.svg")), tr("&Debug"), this, &MainWindow::debugTarget);
    m_debugAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F9));
    m_debugAction->setToolTip(tr("Build the chosen target in build-debug and debug it (Shift+F9)"));
    m_debugConfigureAction = debug->addAction(tr("Debug CMake &Configure"), this, &MainWindow::debugConfigure);
    m_debugConfigureAction->setToolTip(tr("Configure the project in CMake's debugger: breakpoints in CMakeLists.txt"));
    debug->addSeparator();
    m_resumeAction = debug->addAction(tr("&Resume"), this, running(&Debugger::resume));
    m_resumeAction->setShortcut(QKeySequence(Qt::Key_F9));
    m_pauseAction = debug->addAction(tr("&Pause"), this, running(&Debugger::pause));
    m_stepOverAction = debug->addAction(tr("Step &Over"), this, running(&Debugger::stepOver));
    m_stepOverAction->setShortcut(QKeySequence(Qt::Key_F8));
    m_stepInAction = debug->addAction(tr("Step &In"), this, running(&Debugger::stepIn));
    m_stepInAction->setShortcut(QKeySequence(Qt::Key_F7));
    m_stepOutAction = debug->addAction(tr("Step O&ut"), this, running(&Debugger::stepOut));
    m_stepOutAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8));
    m_stopDebugAction = debug->addAction(tr("&Stop Debugging"), this, running(&Debugger::stop));
    for (QAction* action : {m_resumeAction, m_pauseAction, m_stepOverAction, m_stepInAction, m_stepOutAction, m_stopDebugAction})
        action->setToolTip(action->shortcut().isEmpty()
                               ? action->text().remove(QLatin1Char('&'))
                               : QStringLiteral("%1 (%2)").arg(action->text().remove(QLatin1Char('&')),
                                                               action->shortcut().toString()));
    m_debugPanel->setButtons({m_resumeAction, m_pauseAction, m_stepOverAction, m_stepInAction, m_stepOutAction, m_stopDebugAction});
    m_buildBar->insertAction(m_stopAction, m_debugAction);

    m_debugDock = new QDockWidget(tr("Debug"), this);
    m_debugDock->setObjectName(QStringLiteral("debugDock"));
    m_debugDock->setWidget(m_debugPanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_debugDock);
    tabifyDockWidget(m_buildDock, m_debugDock);
    m_debugDock->hide();
    debug->addSeparator();
    debug->addAction(m_debugDock->toggleViewAction());
    updateBuildActions();
}

bool MainWindow::canDebug(DebugRequest::Kind kind) {
    if (!m_debugger) {
        QMessageBox::critical(this, tr("Debugger Plugin Missing"),
                              m_plugins.missingMessage(QStringLiteral(EUDAIMONIA_DEBUGGER_IID), tr("Debugging")));
        return false;
    }
    const QString reason = m_debugger->unavailableReason(kind);
    if (!reason.isEmpty()) QMessageBox::warning(this, tr("Debugging"), reason);
    return reason.isEmpty();
}

void MainWindow::debugTarget() {
    if (!m_buildSystem || m_buildPanel->isRunning() || !canDebug(DebugRequest::Kind::Program) || !saveAll()) return;
    const QString name = m_targetBox->currentText();
    if (name.isEmpty()) return;
    const QString directoryName = QStringLiteral("build-debug");
    configureDirectory(directoryName, false, [this, name, directoryName](bool configured) {
        if (m_buildSystem && directoryName == m_profileBox->currentData().toString()) loadTargets();
        if (!configured || !m_buildSystem) return;
        const QString directory = QDir(m_projectDirectory).filePath(directoryName);
        QString error;
        QString program;
        for (const BuildTarget& target : m_buildSystem->targets(directory, &error))
            if (target.name == name) program = target.executable;
        if (program.isEmpty()) {
            statusBar()->showMessage(error.isEmpty() ? tr("%1 is not a target of %2/").arg(name, directoryName) : error, 10000);
            return;
        }
        m_debugAfterBuild = program;
        m_runName = name;
        m_buildDock->show();
        m_buildPanel->start(m_buildSystem->buildCommand(directory, name, buildOptions()),
                            tr("Build %1 for debugging, %2").arg(name, threadsText()));
    });
}

void MainWindow::debugConfigure() {
    if (!m_buildSystem || m_configuring || !canDebug(DebugRequest::Kind::CMakeConfigure) || !saveAll()) return;
    QString done;
    const BuildCommand command = configureCommand(m_profileBox->currentData().toString(), false, &done);
    m_debuggingConfigure = true;
    startDebugging({DebugRequest::Kind::CMakeConfigure, command.program, command.arguments, command.workingDirectory},
                   tr("Configure %1").arg(QFileInfo(m_projectDirectory).fileName()));
}

void MainWindow::startDebugging(const DebugRequest& request, const QString& name) {
    m_debugStopped = false;
    m_debugPanel->sessionStarted(name);
    m_debugDock->show();
    m_debugDock->raise();
    m_debugger->start(request);  // a failure comes through the listener
    updateBuildActions();
}

void MainWindow::stopDebugging() {
    m_debugAfterBuild.clear();
    if (m_debugger && m_debugger->isRunning()) m_debugger->stop();
}

namespace {

// The setting holding the breakpoints of a project.
QString breakpointsKey(const QString& project) {
    return QStringLiteral("breakpoints/") + QString::fromLatin1(project.toUtf8().toHex());
}

QList<int> fromOne(const QList<int>& fromZero) {
    QList<int> lines;
    lines.reserve(fromZero.size());
    for (const int line : fromZero) lines << line + 1;
    return lines;
}

} // namespace

void MainWindow::loadBreakpoints() {
    if (!m_debugger) return;
    for (auto it = m_breakpoints.cbegin(); it != m_breakpoints.cend(); ++it) m_debugger->setBreakpoints(it.key(), {});
    m_breakpoints.clear();
    const QVariantMap saved = QSettings().value(breakpointsKey(m_projectDirectory)).toMap();
    for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
        QList<int> lines;
        for (const QVariant& line : it.value().toList()) lines << line.toInt();
        m_breakpoints.insert(it.key(), lines);
        m_debugger->setBreakpoints(it.key(), fromOne(lines));
    }
    for (const auto& [page, editor] : m_editors) editor->view->setBreakpoints(m_breakpoints.value(editor->view->filePath()));
}

void MainWindow::saveBreakpoints() const {
    if (m_projectDirectory.isEmpty()) return;
    QVariantMap saved;
    for (auto it = m_breakpoints.cbegin(); it != m_breakpoints.cend(); ++it) {
        QVariantList lines;
        for (const int line : it.value()) lines << line;
        saved.insert(it.key(), lines);
    }
    QSettings().setValue(breakpointsKey(m_projectDirectory), saved);
}

void MainWindow::setFileBreakpoints(const QString& file, const QList<int>& lines) {
    if (lines.isEmpty()) m_breakpoints.remove(file);
    else m_breakpoints.insert(file, lines);
    saveBreakpoints();
    if (m_debugger) m_debugger->setBreakpoints(file, fromOne(lines));
}

void MainWindow::showBreakpoints(Editor& editor) {
    const QString file = editor.view->filePath();
    // Saved under another name: they stay with the text.
    if (!editor.breakpointFile.isEmpty() && editor.breakpointFile != file) {
        const QList<int> lines = m_breakpoints.value(editor.breakpointFile);
        setFileBreakpoints(editor.breakpointFile, {});
        setFileBreakpoints(file, lines);
    }
    editor.breakpointFile = file;
    editor.view->setBreakpointMargin(m_debugger && canHaveBreakpoints(file));
    editor.view->setBreakpoints(m_breakpoints.value(file));
}

// The debugger took a breakpoint on another line, e.g. the next one with code:
// the margin shows where it is. The debugger needs not be told.
void MainWindow::breakpointMoved(const DebugBreakpoint& breakpoint) {
    if (breakpoint.line <= 0 || breakpoint.line == breakpoint.requestedLine) return;
    const auto found = m_breakpoints.find(breakpoint.file);
    if (found == m_breakpoints.end() || !found->contains(breakpoint.requestedLine - 1)) return;
    QList<int> lines = found.value();
    lines.removeOne(breakpoint.requestedLine - 1);
    if (!lines.contains(breakpoint.line - 1)) lines << breakpoint.line - 1;
    std::sort(lines.begin(), lines.end());
    m_breakpoints.insert(breakpoint.file, lines);
    saveBreakpoints();
    if (spp::EditorView* view = viewOf(m_tabs->findTab(breakpoint.file))) view->setBreakpoints(lines);
}

void MainWindow::showExecution(const QString& file, int line) {
    clearExecution();
    if (file.isEmpty()) return;
    openLocation(file, line, 0);
    spp::EditorView* view = viewOf(m_tabs->currentWidget());
    if (!view || view->filePath() != canonicalPath(file)) return;  // it could not be opened
    // Stopped in it: whatever the kind of file, the line shows.
    view->setBreakpointMargin(true);
    view->setExecutionLine(line - 1);
    m_executionFile = view->filePath();
}

void MainWindow::clearExecution() {
    if (m_executionFile.isEmpty()) return;
    if (spp::EditorView* view = viewOf(m_tabs->findTab(m_executionFile))) view->setExecutionLine(-1);
    m_executionFile.clear();
}

void MainWindow::runProgram(const QString& program, const QString& name) {
    // The program runs in its build directory, as CLion runs it.
    const QString directory = QFileInfo(program).absolutePath();
    if (m_runInTerminal->isChecked()) {
        const QString script =
            QStringLiteral("cd %1 && %2").arg(Terminal::quote(directory), Terminal::command(program, {}));
        const QString title = tr("Run %1").arg(name);
        QString error;
        if (!Terminal::run(script, m_projectDirectory, title, &error)) QMessageBox::warning(this, title, error);
        return;
    }
    m_runDock->show();
    m_runDock->raise();
    if (m_runPanel->isRunning()) {
        // Run again: the running one stops, the new one starts after it.
        m_pendingProgram = program;
        m_pendingName = name;
        m_runPanel->stop();
        return;
    }
    m_runPanel->start(program, {}, directory);
}

void MainWindow::setUpLanguages() {
    m_language = m_plugins.plugin<LanguageSupport>();
    if (m_language) {
        m_languageListener = std::make_unique<LanguageListener>();
        m_languageListener->window = this;
        m_language->setListener(m_languageListener.get());
    }

    // The problems the servers found, each opening its place.
    m_problems = new QListWidget;
    auto open = [this](QListWidgetItem* item) {
        openLocation(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toInt(),
                     item->data(Qt::UserRole + 2).toInt());
    };
    connect(m_problems, &QListWidget::itemActivated, this, open);
    connect(m_problems, &QListWidget::itemClicked, this, open);
    m_problemsDock = new QDockWidget(tr("Problems"), this);
    m_problemsDock->setObjectName(QStringLiteral("problemsDock"));
    m_problemsDock->setWidget(m_problems);
    addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);
    tabifyDockWidget(m_buildDock, m_problemsDock);
    m_problemsDock->hide();

    QMenu* navigate = menuBar()->insertMenu(menuBar()->actions().last(), new QMenu(tr("&Navigate"), this))->menu();
    m_backAction = navigate->addAction(tr("&Back"), this, &MainWindow::navigateBack);
    m_backAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left));
    m_forwardAction = navigate->addAction(tr("&Forward"), this, &MainWindow::navigateForward);
    m_forwardAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right));
    navigate->addSeparator();
    QAction* definition = navigate->addAction(tr("Go to &Definition"), this, &MainWindow::goToDefinition);
    definition->setShortcut(QKeySequence(Qt::Key_F12));
    navigate->addAction(m_problemsDock->toggleViewAction());
    updateNavigationActions();
}

void MainWindow::restartLanguages() {
    m_languageBuildDirectory = buildDirectory();
    m_language->setProject(m_projectDirectory, m_languageBuildDirectory);
    m_diagnostics.clear();
    updateProblems();
    for (auto& [page, editor] : m_editors) {
        editor->languageFile.clear();
        languageOpened(*editor);
    }
}

NavigationHistory::Place MainWindow::placeOf(QWidget* page) const {
    const auto editor = m_editors.find(page);
    if (editor == m_editors.end()) return {};
    const spp::TextPosition cursor = editor->second->view->cursorPosition();
    return {editor->second->view->filePath(), cursor.line, cursor.column};
}

void MainWindow::navigateBack() {
    if (m_history.canGoBack()) goToPlace(m_history.back(placeOf(m_tabs->currentWidget())));
}

void MainWindow::navigateForward() {
    if (m_history.canGoForward()) goToPlace(m_history.forward(placeOf(m_tabs->currentWidget())));
}

void MainWindow::goToPlace(const NavigationHistory::Place& place) {
    m_navigating = true;
    // A tab closed since gets opened again.
    if (openFile(place.file)) {
        if (spp::EditorView* view = viewOf(m_tabs->currentWidget())) {
            view->setCursorPosition({place.line, place.column});
            view->widget()->setFocus();
        }
    }
    m_previousTab = m_tabs->currentWidget();
    m_navigating = false;
    updateNavigationActions();
}

void MainWindow::updateNavigationActions() {
    if (!m_backAction) return;
    m_backAction->setEnabled(m_history.canGoBack());
    m_forwardAction->setEnabled(m_history.canGoForward());
}

void MainWindow::languageOpened(Editor& editor) {
    const QString file = editor.view->filePath();
    if (!m_language || file.isEmpty() || !m_language->handles(file)) return;
    editor.languageFile = file;
    m_language->opened(file, editor.view->text());
}

void MainWindow::languageClosed(Editor& editor) {
    if (!m_language || editor.languageFile.isEmpty()) return;
    const QString file = editor.languageFile;
    editor.languageFile.clear();
    m_language->closed(file);  // its diagnostics go with it
}

void MainWindow::showDiagnostics(const QString& file, const QList<Diagnostic>& list) {
    if (list.isEmpty()) m_diagnostics.remove(file);
    else m_diagnostics.insert(file, list);
    for (const auto& [page, editor] : m_editors) {
        if (editor->languageFile != file) continue;
        QList<spp::TextMark> marks;
        for (const Diagnostic& diagnostic : list) {
            spp::TextMark mark;
            mark.start = {diagnostic.start.line, diagnostic.start.column};
            mark.end = {diagnostic.end.line, diagnostic.end.column};
            mark.kind = diagnostic.severity == Diagnostic::Severity::Error     ? spp::TextMark::Kind::Error
                        : diagnostic.severity == Diagnostic::Severity::Warning ? spp::TextMark::Kind::Warning
                        : diagnostic.severity == Diagnostic::Severity::Hint    ? spp::TextMark::Kind::Hint
                                                                               : spp::TextMark::Kind::Information;
            mark.message = diagnostic.source.isEmpty() ? diagnostic.message
                                                       : diagnostic.source + QStringLiteral(": ") + diagnostic.message;
            marks << mark;
        }
        editor->view->setMarks(marks);
    }
    updateProblems();
}

void MainWindow::updateProblems() {
    m_problems->clear();
    int errors = 0;
    int warnings = 0;
    QStringList files = m_diagnostics.keys();
    files.sort();
    for (const QString& file : files) {
        for (const Diagnostic& diagnostic : m_diagnostics.value(file)) {
            const bool error = diagnostic.severity == Diagnostic::Severity::Error;
            const bool warning = diagnostic.severity == Diagnostic::Severity::Warning;
            errors += error;
            warnings += warning;
            const QString shown = !m_projectDirectory.isEmpty() && file.startsWith(m_projectDirectory + '/')
                                      ? QDir(m_projectDirectory).relativeFilePath(file)
                                      : file;
            auto* item = new QListWidgetItem(QStringLiteral("%1:%2:%3: %4")
                                                 .arg(shown)
                                                 .arg(diagnostic.start.line + 1)
                                                 .arg(diagnostic.start.column + 1)
                                                 .arg(diagnostic.message.section(QLatin1Char('\n'), 0, 0)));
            item->setToolTip(diagnostic.message);
            item->setForeground(error ? QColor(0xc0, 0x10, 0x10) : warning ? QColor(0xa0, 0x60, 0x00) : QColor(0x40, 0x60, 0x90));
            item->setData(Qt::UserRole, file);
            item->setData(Qt::UserRole + 1, diagnostic.start.line + 1);
            item->setData(Qt::UserRole + 2, diagnostic.start.column + 1);
            m_problems->addItem(item);
        }
    }
    m_problemsDock->setWindowTitle(errors || warnings ? tr("Problems (%1 errors, %2 warnings)").arg(errors).arg(warnings)
                                                      : tr("Problems"));
}

void MainWindow::selectWord() {
    spp::EditorView* view = viewOf(m_tabs->currentWidget());
    if (!view) return;
    const spp::TextPosition cursor = view->cursorPosition();
    const QString line = view->text().section(QLatin1Char('\n'), cursor.line, cursor.line);
    auto inWord = [&line](int column) {
        if (column < 0 || column >= line.size()) return false;
        const QChar c = line.at(column);
        return c.isLetterOrNumber() || c.isMark() || c.isSurrogate() || c == QLatin1Char('_');
    };
    // The word the cursor is in, or the one it stands right after.
    int start = inWord(cursor.column) ? cursor.column : cursor.column - 1;
    if (!inWord(start)) return;
    int end = start + 1;
    while (inWord(start - 1)) --start;
    while (inWord(end)) ++end;
    view->setSelection({{cursor.line, start}, {cursor.line, end}});
}

void MainWindow::completeCode() {
    const auto editor = m_editors.find(m_tabs->currentWidget());
    if (editor == m_editors.end()) return;
    if (!m_language) {
        QMessageBox::critical(this, tr("Language Support Plugin Missing"),
                              m_plugins.missingMessage(QStringLiteral(EUDAIMONIA_LANGUAGESUPPORT_IID), tr("Code completion")));
        return;
    }
    if (editor->second->languageFile.isEmpty()) {
        statusBar()->showMessage(tr("No language server for %1").arg(QFileInfo(editor->second->view->filePath()).fileName()),
                                 5000);
        return;
    }
    requestCompletions(*editor->second, true);
}

void MainWindow::requestCompletions(Editor& editor, bool asked) {
    if (!m_language || editor.languageFile.isEmpty()) return;
    const QString file = editor.languageFile;
    const spp::TextPosition cursor = editor.view->cursorPosition();
    // The key just typed may not have been told yet.
    m_language->changed(file, editor.view->text());
    const int request = ++m_completionRequest;
    QPointer<QWidget> asker = editor.page;
    m_language->complete(file, {cursor.line, cursor.column}, [this, asker, request, cursor, asked](const QList<Completion>& found, bool incomplete) {
        if (!asker || request != m_completionRequest) return;
        spp::EditorView* view = viewOf(asker);
        if (!view) return;
        if (found.isEmpty() && asked) statusBar()->showMessage(tr("No completions here"), 3000);
        QList<spp::CompletionItem> items;
        items.reserve(found.size());
        for (const Completion& completion : found) {
            spp::CompletionItem item{completion.label, completion.detail, completion.filterText, completion.insertText, {}};
            for (const SourceEdit& edit : completion.additionalEdits)
                item.additionalEdits.append({{{edit.start.line, edit.start.column}, {edit.end.line, edit.end.column}}, edit.text});
            items << item;
        }
        // Where the server says the word starts, else where it starts here.
        spp::TextPosition start{cursor.line, cursor.column};
        if (!found.isEmpty() && found.first().hasStart && found.first().start.line == cursor.line
            && found.first().start.column <= cursor.column) {
            start.column = found.first().start.column;
        } else {
            const QString line = view->text().section(QLatin1Char('\n'), cursor.line, cursor.line);
            while (start.column > 0 && start.column <= line.size()
                   && (line.at(start.column - 1).isLetterOrNumber() || line.at(start.column - 1) == QLatin1Char('_')))
                --start.column;
        }
        view->showCompletions(items, start, incomplete);
    });
}

void MainWindow::goToDefinition() {
    const auto editor = m_editors.find(m_tabs->currentWidget());
    if (editor == m_editors.end()) return;
    if (!m_language) {
        QMessageBox::critical(this, tr("Language Support Plugin Missing"),
                              m_plugins.missingMessage(QStringLiteral(EUDAIMONIA_LANGUAGESUPPORT_IID), tr("Go to Definition")));
        return;
    }
    if (editor->second->languageFile.isEmpty()) {
        statusBar()->showMessage(tr("No language server for %1").arg(QFileInfo(editor->second->view->filePath()).fileName()),
                                 5000);
        return;
    }
    const spp::TextPosition cursor = editor->second->view->cursorPosition();
    statusBar()->showMessage(tr("Looking for the definition..."));
    m_language->findDefinition(editor->second->languageFile, {cursor.line, cursor.column},
                               [this](const QList<SourceLocation>& locations) {
                                   if (locations.isEmpty()) {
                                       statusBar()->showMessage(tr("No definition found"), 5000);
                                       return;
                                   }
                                   statusBar()->clearMessage();
                                   const SourceLocation& first = locations.first();
                                   openLocation(first.file, first.position.line + 1, first.position.column + 1);
                               });
}

void MainWindow::openLocation(const QString& file, int line, int column) {
    // A jump within the file: the place left is kept here (another file's
    // tab is left through the tab change).
    if (!m_navigating && QDir::cleanPath(file) == placeOf(m_tabs->currentWidget()).file) {
        m_history.leave(placeOf(m_tabs->currentWidget()));
        updateNavigationActions();
    }
    if (!openFile(file)) return;
    spp::EditorView* view = viewOf(m_tabs->currentWidget());
    if (!view) return;
    // The jump is recorded already: the cursor's move is not another one.
    const bool navigating = m_navigating;
    m_navigating = true;
    view->setCursorPosition({line - 1, std::max(0, column - 1)});
    m_navigating = navigating;
    view->widget()->setFocus();
}

void MainWindow::chooseProject() {
    const QString start = m_projectDirectory.isEmpty() ? QDir::homePath() : m_projectDirectory;
    const QString directory =
        QxFileDialog::getExistingDirectory(this, tr("Open Project Directory"), start, m_recent.paths());
    if (!directory.isEmpty()) openProject(directory);
}

void MainWindow::chooseFile() {
    QString start = m_lastFileDirectory;
    if (start.isEmpty()) start = m_projectDirectory.isEmpty() ? QDir::homePath() : m_projectDirectory;
    const QStringList files = QxFileDialog::getOpenFileNames(this, tr("Open File"), start);
    for (const QString& path : files) openFile(path);
}

void MainWindow::updateRecentProjects() {
    m_recentMenu->clear();
    m_recentList->clear();
    for (const QString& path : m_recent.paths()) {
        m_recentMenu->addAction(path, this, [this, path] { openProject(path); });
        auto* item = new QListWidgetItem(QStringLiteral("%1 — %2").arg(QFileInfo(path).fileName(), path));
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        m_recentList->addItem(item);
    }
    m_recentMenu->setEnabled(!m_recent.paths().isEmpty());
    if (m_recentList->count() > 0) m_recentList->setCurrentRow(0);
}

void MainWindow::updateTitle() {
    QStringList parts;
    if (spp::EditorView* view = viewOf(m_tabs->currentWidget()))
        parts << QFileInfo(view->filePath()).fileName() + (view->isModified() ? QStringLiteral(" *") : QString());
    if (!m_projectDirectory.isEmpty()) parts << QFileInfo(m_projectDirectory).fileName();
    parts << QStringLiteral("Eudaimonia");
    setWindowTitle(parts.join(QStringLiteral(" — ")));
}

void MainWindow::showPlugins() {
    QStringList lines{tr("Searched directories:")};
    for (const QString& directory : PluginManager::directories()) lines << QStringLiteral("  ") + directory;
    lines << QString() << tr("Plugins:");
    if (m_plugins.report().isEmpty()) lines << tr("  none found");
    for (const QString& line : m_plugins.report()) lines << QStringLiteral("  ") + line;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Plugins"));
    auto* text = new QPlainTextEdit(lines.join('\n'));
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(text);
    layout->addWidget(buttons);
    dialog.resize(700, 400);
    dialog.exec();
}

void MainWindow::restoreState() {
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    QMainWindow::restoreState(settings.value("window/state").toByteArray());
    m_splitter->restoreState(settings.value("window/splitter").toByteArray());
}

void MainWindow::saveState() const {
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", QMainWindow::saveState());
    settings.setValue("window/splitter", m_splitter->saveState());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // A running build is stopped with the window (BuildPanel's destructor).
    const bool debugging = m_debugger && m_debugger->isRunning();
    if ((m_buildPanel->isRunning() || m_runPanel->isRunning() || debugging)
        && QMessageBox::question(this, tr("Still Running"),
                                 m_buildPanel->isRunning() ? tr("A build is running. Stop it and quit?")
                                 : debugging               ? tr("A program is being debugged. Stop it and quit?")
                                                           : tr("The program is running. Stop it and quit?"))
               != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    // Each tab with unsaved changes asks first; Cancel keeps the window.
    if (!m_tabs->requestCloseAllTabs()) {
        event->ignore();
        return;
    }
    saveState();
    event->accept();
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        checkExternalChanges();
        // A build in the terminal may have configured the project again.
        if (m_buildSystem && !m_configuring) loadTargets();
    }
}

} // namespace eudaimonia
