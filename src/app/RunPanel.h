// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QElapsedTimer>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QToolButton;

namespace eudaimonia {

// Runs a program and shows what it writes (its errors in red), takes lines
// for its standard input, and at the end tells its exit status, how long it
// ran and when it finished. Programs that need a terminal (full-screen ones)
// are run in a terminal window instead (see MainWindow).
class RunPanel : public QWidget {
    Q_OBJECT
public:
    explicit RunPanel(QWidget* parent = nullptr);
    ~RunPanel() override;

    bool isRunning() const { return m_process != nullptr; }
    // Starts `program` in `workingDirectory`; false while one runs.
    bool start(const QString& program, const QStringList& arguments, const QString& workingDirectory);
    // Stops the program and all it started.
    void stop();
    QString output() const;

signals:
    void started();
    void finished(int exitCode, bool crashed);

private:
    void append(const QByteArray& bytes, bool error);
    void sendInput();
    void end(int exitCode, bool crashed);
    void kill(int signal);

    QLabel* m_status = nullptr;
    QToolButton* m_stopButton = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QLineEdit* m_input = nullptr;
    QToolButton* m_endInput = nullptr;
    QProcess* m_process = nullptr;
    QString m_name;
    QElapsedTimer m_elapsed;
    bool m_stopped = false;
};

} // namespace eudaimonia
