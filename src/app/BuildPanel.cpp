// SPDX-License-Identifier: GPL-3.0-only
#include "BuildPanel.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <csignal>

#include <sys/types.h>
#include <unistd.h>

namespace eudaimonia {

BuildPanel::BuildPanel(QWidget* parent) : QWidget(parent) {
    m_status = new QLabel;
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stopButton = new QToolButton;
    m_stopButton->setText(tr("Stop"));
    m_stopButton->setToolTip(tr("Stop the build (Ctrl+F2)"));
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QToolButton::clicked, this, &BuildPanel::stop);

    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setMaximumBlockCount(100000);

    auto* top = new QHBoxLayout;
    top->addWidget(m_status, 1);
    top->addWidget(m_stopButton);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(top);
    layout->addWidget(m_output);

    // Memory spikes fast while linking: looked at four times a second.
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &BuildPanel::tick);
}

// A build still running when the IDE closes goes with it.
BuildPanel::~BuildPanel() {
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        kill(SIGKILL);
        m_process->waitForFinished(3000);
    }
}

bool BuildPanel::start(const BuildCommand& command, const QString& title) {
    if (m_process) return false;
    m_title = title;
    m_stopReason.clear();
    m_resident = 0;
    m_peak = 0;
    m_output->clear();
    m_output->appendPlainText(QStringLiteral("$ %1 %2\n").arg(command.program, command.arguments.join(QLatin1Char(' '))));

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setWorkingDirectory(command.workingDirectory);
    // A process group of its own, so that the whole build (cmake, ninja,
    // compilers, linkers) can be stopped at once.
    m_process->setChildProcessModifier([] { ::setsid(); });
    connect(m_process, &QProcess::readyReadStandardOutput, this, &BuildPanel::readOutput);
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        end(exitCode, status == QProcess::CrashExit);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_output->appendPlainText(tr("Cannot start %1: %2").arg(m_process->program(), m_process->errorString()));
        end(-1, true);
    });

    m_startMemory = BuildMonitor::readMemory();
    m_memory = m_startMemory;
    m_limits = m_hasFixedLimits ? m_fixedLimits : BuildMonitor::defaultLimits(m_startMemory);
    m_elapsed.start();
    m_timer.start();
    m_stopButton->setEnabled(true);
    m_process->start(command.program, command.arguments);
    updateStatus();
    emit started();
    return true;
}

QString BuildPanel::output() const {
    return m_output->toPlainText();
}

void BuildPanel::stop() {
    if (!m_process || !m_stopReason.isEmpty()) return;
    m_stopReason = tr("by the user");
    kill(SIGTERM);
    // What does not end by itself within a few seconds is killed.
    QTimer::singleShot(3000, this, [this] {
        if (m_process) kill(SIGKILL);
    });
}

void BuildPanel::kill(int signal) {
    if (!m_process || m_process->processId() <= 0) return;
    // The process group led by the build's first process.
    ::kill(-pid_t(m_process->processId()), signal);
}

void BuildPanel::readOutput() {
    const QString text = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    QScrollBar* bar = m_output->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 2;
    QTextCursor cursor(m_output->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    if (atEnd) bar->setValue(bar->maximum());
}

void BuildPanel::tick() {
    if (!m_process) return;
    m_memory = BuildMonitor::readMemory();
    m_resident = BuildMonitor::treeResident(m_process->processId());
    m_peak = std::max(m_peak, m_resident);
    if (m_stopReason.isEmpty()) {
        const QString reason = BuildMonitor::stopReason(m_memory, m_startMemory, m_limits);
        if (!reason.isEmpty()) {
            // At once and for good: a computer short of memory gets slower
            // with every moment the build keeps running.
            m_stopReason = tr("for lack of memory — %1 —").arg(reason);
            kill(SIGKILL);
        }
    }
    updateStatus();
}

void BuildPanel::updateStatus() {
    QString text = QStringLiteral("<b>%1</b> — %2").arg(m_title.toHtmlEscaped(), duration(m_elapsed.elapsed()));
    text += tr(" — the build takes %1 (at most %2), %3 of memory left")
                .arg(BuildMonitor::formatBytes(m_resident), BuildMonitor::formatBytes(m_peak),
                     BuildMonitor::formatBytes(m_memory.available));
    if (m_memory.swapTotal > 0)
        text += tr(", swap in use %1").arg(BuildMonitor::formatBytes(m_memory.swapUsed()));
    m_status->setText(text);
}

void BuildPanel::end(int exitCode, bool crashed) {
    if (!m_process) return;
    m_timer.stop();
    readOutput();
    const qint64 took = m_elapsed.elapsed();
    const QString finishedAt = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const bool success = m_stopReason.isEmpty() && !crashed && exitCode == 0;
    QString summary;
    if (success)
        summary = tr("Built in %1, finished at %2.").arg(duration(took), finishedAt);
    else if (!m_stopReason.isEmpty())
        summary = tr("Build stopped %1 after %2, at %3.").arg(m_stopReason, duration(took), finishedAt);
    else
        summary = tr("Build failed (exit code %1) after %2, at %3.").arg(exitCode).arg(duration(took), finishedAt);
    summary += tr(" The build took at most %1 of memory.").arg(BuildMonitor::formatBytes(m_peak));
    m_output->appendPlainText(QStringLiteral("\n") + summary);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
    m_status->setText(QStringLiteral("<b>%1</b> — %2").arg(m_title.toHtmlEscaped(), summary.toHtmlEscaped()));
    m_stopButton->setEnabled(false);
    m_process->deleteLater();
    m_process = nullptr;
    emit finished(success);
}

QString BuildPanel::duration(qint64 milliseconds) {
    const qint64 seconds = milliseconds / 1000;
    if (seconds < 60) return tr("%1 s").arg(double(milliseconds) / 1000, 0, 'f', 1);
    if (seconds < 3600) return tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60);
    return tr("%1 h %2 min").arg(seconds / 3600).arg(seconds % 3600 / 60);
}

} // namespace eudaimonia
