// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"

#include "build_info.h"

#include <mrutabwidget.h>
#include <qxfiledialog.h>
#include <siplasplas/plugin/EditorPlugin.h>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
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
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
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
    QAction* saveAll = file->addAction(tr("Save A&ll"), this, [this] {
        for (const auto& [page, editor] : m_editors)
            if (editor->view->isModified()) saveTab(page);
    });
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
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) checkExternalChanges();
}

} // namespace eudaimonia
