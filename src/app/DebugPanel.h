// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QWidget>

class QAction;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace eudaimonia {

class Debugger;
struct DebugFrame;
struct DebugVariable;

// A debugging session: the debugger's buttons, the call stack where it
// stopped, the variables of the frame chosen (members asked for when opened)
// and what the program and the debugger write. The window tells it what the
// debugger does.
class DebugPanel : public QWidget {
    Q_OBJECT
public:
    explicit DebugPanel(QWidget* parent = nullptr);

    void setDebugger(Debugger* debugger) { m_debugger = debugger; }
    void setButtons(const QList<QAction*>& actions);

    void sessionStarted(const QString& name);
    void stopped(int threadId, const QString& reason);
    void continued();
    void append(const QString& text);
    void sessionEnded(int exitCode, const QString& error);

    QString output() const;

signals:
    // The frame to show: a file (empty without source) and a line from 1.
    void frameChosen(const QString& file, int line);

private:
    void showFrames(const QList<DebugFrame>& frames);
    void chooseFrame(int row);
    void addVariables(QTreeWidgetItem* parent, const QList<DebugVariable>& variables);
    void expanded(QTreeWidgetItem* item);
    void clearStop();

    Debugger* m_debugger = nullptr;
    QLabel* m_status = nullptr;
    QToolBar* m_buttons = nullptr;
    QListWidget* m_frames = nullptr;
    QTreeWidget* m_variables = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QString m_name;
    QList<DebugFrame> m_shownFrames;
    // Answers for a stop or a frame left since are let go.
    int m_stop = 0;
    int m_frame = 0;
};

} // namespace eudaimonia
