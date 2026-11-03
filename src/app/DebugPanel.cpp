// SPDX-License-Identifier: GPL-3.0-only
#include "DebugPanel.h"

#include <eudaimonia/plugin/Debugger.h>

#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace eudaimonia {
namespace {

// An item's variablesReference, and whether its children were asked for.
constexpr int referenceRole = Qt::UserRole;
constexpr int askedRole = Qt::UserRole + 1;

} // namespace

DebugPanel::DebugPanel(QWidget* parent) : QWidget(parent) {
    m_status = new QLabel;
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_buttons = new QToolBar;
    m_buttons->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_frames = new QListWidget;
    connect(m_frames, &QListWidget::currentRowChanged, this, &DebugPanel::chooseFrame);

    m_variables = new QTreeWidget;
    m_variables->setHeaderLabels({tr("Name"), tr("Value"), tr("Type")});
    m_variables->header()->setStretchLastSection(true);
    connect(m_variables, &QTreeWidget::itemExpanded, this, &DebugPanel::expanded);

    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setMaximumBlockCount(100000);

    auto* parts = new QSplitter(Qt::Horizontal);
    parts->addWidget(m_frames);
    parts->addWidget(m_variables);
    parts->addWidget(m_output);
    parts->setSizes({250, 350, 400});

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(m_buttons);
    top->addWidget(m_status, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(top);
    layout->addWidget(parts, 1);
}

void DebugPanel::setButtons(const QList<QAction*>& actions) {
    m_buttons->addActions(actions);
}

QString DebugPanel::output() const {
    return m_output->toPlainText();
}

void DebugPanel::clearStop() {
    ++m_stop;
    ++m_frame;
    m_shownFrames.clear();
    const QSignalBlocker blocker(m_frames);
    m_frames->clear();
    m_variables->clear();
}

void DebugPanel::sessionStarted(const QString& name) {
    m_name = name;
    clearStop();
    m_output->clear();
    m_status->setText(tr("Debugging %1...").arg(name));
}

void DebugPanel::stopped(int threadId, const QString& reason) {
    ++m_stop;
    m_status->setText(tr("%1: stopped (%2)").arg(m_name, reason));
    if (!m_debugger) return;
    const int stop = m_stop;
    QPointer<DebugPanel> panel = this;
    m_debugger->stackTrace(threadId, [panel, stop](const QList<DebugFrame>& frames) {
        if (panel && stop == panel->m_stop) panel->showFrames(frames);
    });
}

// While it runs what was shown stays, greyed: a step would make it flicker.
void DebugPanel::continued() {
    ++m_stop;
    ++m_frame;
    m_status->setText(tr("%1: running").arg(m_name));
    m_frames->setEnabled(false);
    m_variables->setEnabled(false);
}

void DebugPanel::showFrames(const QList<DebugFrame>& frames) {
    m_shownFrames = frames;
    m_frames->setEnabled(true);
    m_variables->setEnabled(true);
    int first = -1;
    {
        const QSignalBlocker blocker(m_frames);
        m_frames->clear();
        for (int i = 0; i < frames.size(); ++i) {
            const DebugFrame& frame = frames.at(i);
            const QString place = frame.file.isEmpty() ? tr("no source")
                                                       : QStringLiteral("%1:%2").arg(QFileInfo(frame.file).fileName()).arg(frame.line);
            auto* item = new QListWidgetItem(QStringLiteral("%1   %2").arg(frame.name, place));
            item->setToolTip(frame.file);
            m_frames->addItem(item);
            if (first < 0 && !frame.file.isEmpty()) first = i;
        }
    }
    // Where there is something to read: library code has no source.
    if (!frames.isEmpty()) m_frames->setCurrentRow(std::max(first, 0));
}

void DebugPanel::chooseFrame(int row) {
    if (row < 0 || row >= m_shownFrames.size()) return;
    ++m_frame;
    m_variables->clear();
    const DebugFrame frame = m_shownFrames.at(row);
    emit frameChosen(frame.file, frame.line);
    if (!m_debugger) return;
    const int asked = m_frame;
    QPointer<DebugPanel> panel = this;
    m_debugger->scopes(frame.id, [panel, asked](const QList<DebugScope>& scopes) {
        if (!panel || asked != panel->m_frame) return;
        for (const DebugScope& scope : scopes) {
            auto* item = new QTreeWidgetItem(panel->m_variables, {scope.name});
            item->setData(0, referenceRole, scope.variablesReference);
            item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            // Arguments and locals at once; gdb's registers when asked for.
            if (scope.name != QLatin1String("Registers")) item->setExpanded(true);
        }
    });
}

void DebugPanel::expanded(QTreeWidgetItem* item) {
    if (item->data(0, askedRole).toBool() || !m_debugger) return;
    item->setData(0, askedRole, true);
    const int asked = m_frame;
    QPointer<DebugPanel> panel = this;
    m_debugger->variables(item->data(0, referenceRole).toInt(), [panel, asked, item](const QList<DebugVariable>& variables) {
        // A frame left took the item with it.
        if (panel && asked == panel->m_frame) panel->addVariables(item, variables);
    });
}

void DebugPanel::addVariables(QTreeWidgetItem* parent, const QList<DebugVariable>& variables) {
    for (const DebugVariable& variable : variables) {
        auto* item = new QTreeWidgetItem(parent, {variable.name, variable.value, variable.type});
        item->setToolTip(1, variable.value);
        item->setData(0, referenceRole, variable.variablesReference);
        if (variable.variablesReference > 0) item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
    if (variables.isEmpty()) parent->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
}

void DebugPanel::append(const QString& text) {
    QScrollBar* scroll = m_output->verticalScrollBar();
    const bool atEnd = scroll->value() == scroll->maximum();
    QTextCursor cursor(m_output->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    if (atEnd) scroll->setValue(scroll->maximum());
}

void DebugPanel::sessionEnded(int exitCode, const QString& error) {
    clearStop();
    m_frames->setEnabled(true);
    m_variables->setEnabled(true);
    if (!error.isEmpty()) {
        m_status->setText(error.section(QLatin1Char('\n'), 0, 0));
        append(error + QLatin1Char('\n'));
    } else if (exitCode >= 0) {
        m_status->setText(tr("%1: exited with code %2").arg(m_name).arg(exitCode));
    } else {
        m_status->setText(tr("%1: stopped").arg(m_name));
    }
}

} // namespace eudaimonia
