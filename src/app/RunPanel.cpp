// SPDX-License-Identifier: GPL-3.0-only
#include "RunPanel.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <csignal>

#include <sys/types.h>
#include <unistd.h>

namespace eudaimonia {
namespace {

QString duration(qint64 milliseconds) {
    const qint64 seconds = milliseconds / 1000;
    if (seconds < 60) return QObject::tr("%1 s").arg(double(milliseconds) / 1000, 0, 'f', 1);
    if (seconds < 3600) return QObject::tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60);
    return QObject::tr("%1 h %2 min").arg(seconds / 3600).arg(seconds % 3600 / 60);
}

} // namespace

RunPanel::RunPanel(QWidget* parent) : QWidget(parent) {
    m_status = new QLabel;
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stopButton = new QToolButton;
    m_stopButton->setText(tr("Stop"));
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QToolButton::clicked, this, &RunPanel::stop);

    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setMaximumBlockCount(100000);

    // The program's standard input: a line at a time; End closes it.
    m_input = new QLineEdit;
    m_input->setPlaceholderText(tr("Input for the program: Enter sends the line"));
    m_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_input->setEnabled(false);
    connect(m_input, &QLineEdit::returnPressed, this, &RunPanel::sendInput);
    m_endInput = new QToolButton;
    m_endInput->setText(tr("End Input"));
    m_endInput->setToolTip(tr("Close the program's standard input (end of file)"));
    m_endInput->setEnabled(false);
    connect(m_endInput, &QToolButton::clicked, this, [this] {
        if (m_process) m_process->closeWriteChannel();
        m_input->setEnabled(false);
        m_endInput->setEnabled(false);
    });

    auto* top = new QHBoxLayout;
    top->addWidget(m_status, 1);
    top->addWidget(m_stopButton);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_input, 1);
    bottom->addWidget(m_endInput);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(top);
    layout->addWidget(m_output);
    layout->addLayout(bottom);
}

// A program still running when the IDE closes goes with it.
RunPanel::~RunPanel() {
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        kill(SIGKILL);
        m_process->waitForFinished(3000);
    }
}

bool RunPanel::start(const QString& program, const QStringList& arguments, const QString& workingDirectory) {
    if (m_process) return false;
    m_name = QFileInfo(program).fileName();
    m_stopped = false;
    m_output->clear();
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(workingDirectory);
    // A process group of its own, so that Stop ends what the program started too.
    m_process->setChildProcessModifier([] { ::setsid(); });
    connect(m_process, &QProcess::readyReadStandardOutput, this,
            [this] { append(m_process->readAllStandardOutput(), false); });
    connect(m_process, &QProcess::readyReadStandardError, this,
            [this] { append(m_process->readAllStandardError(), true); });
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        end(exitCode, status == QProcess::CrashExit);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        append(tr("Cannot start %1: %2\n").arg(m_process->program(), m_process->errorString()).toLocal8Bit(), true);
        end(-1, true);
    });
    // Line by line through a pipe, as in a terminal: C's stdio buffers
    // whole blocks otherwise, and the output would come at the end.
    const QString stdbuf = QStandardPaths::findExecutable(QStringLiteral("stdbuf"));
    m_elapsed.start();
    if (stdbuf.isEmpty())
        m_process->start(program, arguments);
    else
        m_process->start(stdbuf, QStringList{QStringLiteral("-oL"), QStringLiteral("-eL"), program} + arguments);
    m_status->setText(tr("<b>%1</b> is running").arg(m_name.toHtmlEscaped()));
    m_stopButton->setEnabled(true);
    m_input->setEnabled(true);
    m_endInput->setEnabled(true);
    m_input->setFocus();
    emit started();
    return true;
}

QString RunPanel::output() const {
    return m_output->toPlainText();
}

void RunPanel::stop() {
    if (!m_process || m_stopped) return;
    m_stopped = true;
    kill(SIGTERM);
    QTimer::singleShot(3000, this, [this] {
        if (m_process) kill(SIGKILL);
    });
}

void RunPanel::kill(int signal) {
    if (!m_process || m_process->processId() <= 0) return;
    ::kill(-pid_t(m_process->processId()), signal);
}

void RunPanel::append(const QByteArray& bytes, bool error) {
    if (bytes.isEmpty()) return;
    QScrollBar* bar = m_output->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 2;
    QTextCursor cursor(m_output->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    if (error) format.setForeground(QColor(0xc0, 0x10, 0x10));
    cursor.insertText(QString::fromLocal8Bit(bytes), format);
    if (atEnd) bar->setValue(bar->maximum());
}

void RunPanel::sendInput() {
    if (!m_process) return;
    const QString line = m_input->text();
    m_input->clear();
    m_process->write((line + QLatin1Char('\n')).toLocal8Bit());
    // What was typed, as a terminal shows it.
    QTextCursor cursor(m_output->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setFontItalic(true);
    format.setForeground(QColor(0x30, 0x60, 0x30));
    cursor.insertText(line + QLatin1Char('\n'), format);
}

void RunPanel::end(int exitCode, bool crashed) {
    if (!m_process) return;
    append(m_process->readAllStandardOutput(), false);
    append(m_process->readAllStandardError(), true);
    const QString finishedAt = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    QString summary;
    if (m_stopped)
        summary = tr("%1 stopped after %2, at %3.").arg(m_name, duration(m_elapsed.elapsed()), finishedAt);
    else if (crashed)
        summary = tr("%1 crashed after %2, at %3.").arg(m_name, duration(m_elapsed.elapsed()), finishedAt);
    else
        summary = tr("%1 exited with code %2 after %3, at %4.")
                      .arg(m_name)
                      .arg(exitCode)
                      .arg(duration(m_elapsed.elapsed()), finishedAt);
    QTextCursor cursor(m_output->document());
    cursor.movePosition(QTextCursor::End);
    if (!m_output->toPlainText().isEmpty() && !m_output->toPlainText().endsWith(QLatin1Char('\n')))
        cursor.insertText(QStringLiteral("\n"));
    QTextCharFormat format;
    format.setFontWeight(QFont::Bold);
    cursor.insertText(QStringLiteral("\n") + summary, format);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
    m_status->setText(summary.toHtmlEscaped());
    m_stopButton->setEnabled(false);
    m_input->setEnabled(false);
    m_endInput->setEnabled(false);
    m_process->deleteLater();
    m_process = nullptr;
    emit finished(exitCode, crashed);
}

} // namespace eudaimonia
