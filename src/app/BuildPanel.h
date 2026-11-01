// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "BuildMonitor.h"

#include <eudaimonia/plugin/BuildSystem.h>

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QProcess;
class QToolButton;

namespace eudaimonia {

// Runs a build command and shows its output, how long it has been running,
// the memory it takes and what the computer has left. A build that would
// leave the computer without memory, or make it swap, is stopped at once
// (its whole process group is killed). At the end it says how long it took
// and when it finished.
class BuildPanel : public QWidget {
    Q_OBJECT
public:
    explicit BuildPanel(QWidget* parent = nullptr);
    ~BuildPanel() override;

    bool isRunning() const { return m_process != nullptr; }
    // Starts `command`, showing `title`; false while another build runs.
    bool start(const BuildCommand& command, const QString& title);
    // Stops the build (asked by the user).
    void stop();
    // The limits of the builds from now on, instead of
    // BuildMonitor::defaultLimits() of the computer's memory.
    void setLimits(const BuildMonitor::Limits& limits) { m_fixedLimits = limits; m_hasFixedLimits = true; }
    // The output so far.
    QString output() const;

signals:
    void started();
    // The build ended: `success` when it exited with 0.
    void finished(bool success);
    // A compiler's message was clicked: its place in a file (absolute path,
    // line and column from 1; column 0 when not given).
    void locationActivated(const QString& file, int line, int column);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The place of the message on the output's line at `position`
    // (viewport coordinates); empty file when there is none.
    QString messageFileAt(const QPoint& position, int* line, int* column) const;
    void readOutput();
    void tick();
    void end(int exitCode, bool crashed);
    void kill(int signal);
    void updateStatus();
    static QString duration(qint64 milliseconds);

    QLabel* m_status = nullptr;
    QToolButton* m_stopButton = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QProcess* m_process = nullptr;
    QString m_title;
    QString m_workingDirectory;  // relative paths in messages are relative to it
    QElapsedTimer m_elapsed;
    QTimer m_timer;
    BuildMonitor::Memory m_startMemory;
    BuildMonitor::Memory m_memory;
    BuildMonitor::Limits m_limits;
    BuildMonitor::Limits m_fixedLimits;
    bool m_hasFixedLimits = false;
    qint64 m_resident = 0;
    qint64 m_peak = 0;
    QString m_stopReason;  // why the build was stopped, empty when it was not
};

} // namespace eudaimonia
