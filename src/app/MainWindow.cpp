// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"

#include <mrutabwidget.h>
#include <qxfiledialog.h>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFontDatabase>
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

// Larger files are not opened: the temporary text view reads a file whole.
constexpr qint64 maxFileSize = 32 * 1024 * 1024;

QString canonicalPath(const QString& path) {
    const QFileInfo info(QxFileDialog::expandHomePath(path));
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

} // namespace

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
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { updateTitle(); });

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

void MainWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* openProject = file->addAction(tr("Open &Project Directory..."), this, &MainWindow::chooseProject);
    openProject->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    QAction* openFile = file->addAction(tr("&Open File..."), this, &MainWindow::chooseFile);
    openFile->setShortcut(QKeySequence::Open);
    m_recentMenu = file->addMenu(tr("&Recent Projects"));
    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&Plugins..."), this, &MainWindow::showPlugins);
    help->addAction(tr("&About Eudaimonia"), this, [this] {
        QMessageBox::about(this, tr("About Eudaimonia"),
                           tr("<b>Eudaimonia</b> %1<br>An integrated development environment.<br>"
                              "Licensed under the GNU General Public License, version 3 only.")
                               .arg(QStringLiteral(EUDAIMONIA_VERSION)));
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
    QFile file(path);
    if (file.size() > maxFileSize) {
        QMessageBox::warning(this, tr("Open File"),
                             tr("%1 is larger than %2 MiB.").arg(path).arg(maxFileSize / (1024 * 1024)));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open File"), tr("Cannot open %1: %2").arg(path, file.errorString()));
        return false;
    }
    if (!m_tabs->makeRoomForNewTab()) return false;

    // Temporary read-only view until the editor plugin replaces it.
    auto* view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setPlainText(QString::fromUtf8(file.readAll()));

    const QFileInfo info(path);
    const int index = m_tabs->addTab(view, info.fileName());
    m_tabs->setTabKey(view, path);
    m_tabs->setTabToolTip(index, path);
    const QString shown = !m_projectDirectory.isEmpty() && path.startsWith(m_projectDirectory + '/')
                              ? QDir(m_projectDirectory).relativeFilePath(path)
                              : path;
    m_tabs->setTabPopupText(index, shown);
    m_tabs->setCurrentIndex(index);
    m_lastFileDirectory = info.absolutePath();
    return true;
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
    if (QWidget* page = m_tabs->currentWidget()) parts << QFileInfo(m_tabs->tabKey(page)).fileName();
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
    saveState();
    event->accept();
}

} // namespace eudaimonia
