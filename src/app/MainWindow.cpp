// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"

#include "Terminal.h"
#include "build_info.h"

#include <mrutabwidget.h>
#include <qxfiledialog.h>
#include <siplasplas/plugin/EditorPlugin.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
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

} // namespace

// A tab's editor, and what it tells the window.
struct MainWindow::Editor : spp::EditorViewListener {
    MainWindow* window = nullptr;
    QWidget* page = nullptr;
    spp::EditorView* view = nullptr;

    void modificationChanged(bool) override { window->updateTabText(page); }
    void filePathChanged(const QString&) override { window->updateTabText(page); }
    void cursorMoved(spp::TextPosition) override {
        if (window->m_tabs->currentWidget() == page) window->updateStatus();
    }
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
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        updateTitle();
        updateStatus();
    });
    connect(m_tabs, &MruTabWidget::tabAboutToClose, this, [this](QWidget* page, bool, bool& allow) {
        if (allow) allow = confirmClose(page);
    });
    connect(m_tabs, &MruTabWidget::tabClosed, this, [this](QWidget* page) {
        const auto editor = m_editors.find(page);
        if (editor == m_editors.end()) return;
        editor->second->view->setListener(nullptr);
        m_editors.erase(editor);
    });

    m_position = new QLabel;
    statusBar()->addPermanentWidget(m_position);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(m_side);
    m_splitter->addWidget(m_tabs);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 900});
    setCentralWidget(m_splitter);

    createMenus();
    createBuildBar();
    updateRecentProjects();
    updateTitle();
    resize(1200, 800);
    restoreState();
}

// The pages may outlive this object's own part: their editors tell no one.
MainWindow::~MainWindow() {
    for (const auto& [page, editor] : m_editors) editor->view->setListener(nullptr);
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
    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

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
    setUpBuild();
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
    m_editors.emplace(page.get(), std::move(editor));
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

} // namespace

void MainWindow::createBuildBar() {
    QToolBar* bar = addToolBar(tr("Build"));
    bar->setObjectName(QStringLiteral("buildBar"));
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
    build->addSeparator();
    m_configureAction = build->addAction(tr("&Configure Project"), this, [this] { configure(); });
    m_backendMenu = build->addMenu(tr("Build &Tool"));
    connect(m_backendMenu, &QMenu::aboutToShow, this, &MainWindow::fillBackendMenu);
    bar->addAction(m_buildAction);
    bar->addAction(m_runAction);
    updateBuildActions();
}

QString MainWindow::buildDirectory() const {
    return QDir(m_projectDirectory).filePath(QStringLiteral("build"));
}

void MainWindow::setUpBuild() {
    if (m_configuring) {
        // The configuration of the project left: its end is of no interest.
        disconnect(m_configuring, nullptr, this, nullptr);
        m_configuring->kill();
        m_configuring->deleteLater();
        m_configuring = nullptr;
    }
    m_buildSystem = nullptr;
    m_targets.clear();
    m_targetBox->clear();
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
        QString state = configured.isEmpty() ? tr("build/ is not configured yet") : tr("build/ uses %1").arg(configured);
        if (!configured.isEmpty() && linkJobs() > 0 && !m_buildSystem->supportsLinkJobs(configured))
            state += tr(", which links as many at once as it compiles");
        m_backendMenu->addAction(state)->setEnabled(false);
    }
}

void MainWindow::chooseBackend(const QString& backend) {
    QSettings().setValue(QStringLiteral("build/backend"), backend);
    if (!m_buildSystem || backend.isEmpty() || m_configuring) return;
    const QString configured = m_buildSystem->configuredBackend(buildDirectory());
    if (configured.isEmpty() || configured == backend) return;
    // Switching rebuilds everything: the open project switches when asked.
    if (QMessageBox::question(this, tr("Build Tool"),
                              tr("build/ of %1 uses %2. Configure it afresh for %3? Everything is built again.")
                                  .arg(QFileInfo(m_projectDirectory).fileName(), configured, backend))
        == QMessageBox::Yes)
        configure(true);
}

void MainWindow::configure(bool switchBackend) {
    if (!m_buildSystem || m_configuring) return;
    // A configured build directory keeps its build tool unless asked to switch.
    const QString configured = m_buildSystem->configuredBackend(buildDirectory());
    const QString chosen = chosenBackend(m_buildSystem);
    const QString backend = configured.isEmpty() || switchBackend ? chosen : QString();
    QString done = tr("Configured %1").arg(QFileInfo(m_projectDirectory).fileName());
    if (!configured.isEmpty() && !chosen.isEmpty() && configured != chosen && !switchBackend)
        done = tr("build/ uses %1, not %2: Build → Build Tool switches it").arg(configured, chosen);
    ConfigureOptions options;
    options.backend = backend;
    options.linkJobs = linkJobs();
    const BuildCommand command = m_buildSystem->configureCommand(m_projectDirectory, buildDirectory(), options);
    auto* process = new QProcess(this);
    m_configuring = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(command.workingDirectory);
    const QString project = m_projectDirectory;
    statusBar()->showMessage(tr("Configuring %1 with %2...").arg(QFileInfo(project).fileName(), m_buildSystem->name()));
    connect(process, &QProcess::finished, this, [this, process, project, done](int exitCode, QProcess::ExitStatus status) {
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
            loadTargets();
            return;
        }
        statusBar()->showMessage(done, 10000);
        loadTargets();
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
    const bool idle = m_buildSystem && !m_configuring;
    m_configureAction->setEnabled(idle);
    m_buildAction->setEnabled(idle);
    m_runAction->setEnabled(idle && m_targetBox->count() > 0);
    m_targetBox->setEnabled(m_targetBox->count() > 0);
}

void MainWindow::buildAndRun(bool run) {
    if (!m_buildSystem) {
        if (!m_plugins.plugin<BuildSystem>())
            QMessageBox::critical(this, tr("Build System Plugin Missing"),
                                  m_plugins.missingMessage(QStringLiteral(EUDAIMONIA_BUILDSYSTEM_IID), tr("Building")));
        return;
    }
    if (!saveAll()) return;
    const int index = m_targetBox->currentIndex();
    const BuildTarget* target = index >= 0 && index < m_targets.size() ? &m_targets[index] : nullptr;
    if (run && !target) return;
    // Without a chosen target Build builds everything.
    const BuildCommand build = m_buildSystem->buildCommand(buildDirectory(), target ? target->name : QString());
    QString script = QStringLiteral("cd %1 && %2")
                         .arg(Terminal::quote(build.workingDirectory), Terminal::command(build.program, build.arguments));
    QString title = tr("Build %1").arg(target ? target->name : QFileInfo(m_projectDirectory).fileName());
    if (run) {
        // The program runs in its build directory, as CLion runs it.
        const QString directory = QFileInfo(target->executable).absolutePath();
        script += QStringLiteral(" && cd %1 && %2")
                      .arg(Terminal::quote(directory), Terminal::command(target->executable, {}));
        title = tr("Run %1").arg(target->name);
    }
    QString error;
    if (!Terminal::run(script, m_projectDirectory, title, &error))
        QMessageBox::warning(this, title, error);
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
