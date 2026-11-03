// SPDX-License-Identifier: GPL-3.0-only
#include "BuildPanel.h"

#include "CompilerMessages.h"

#include <QDateTime>
#include <QDir>
#include <QMouseEvent>
#include <QSyntaxHighlighter>
#include <QTextBlock>
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
namespace {

// The message of a line of the output; the command shown first ("$ ...")
// is none, whatever its words.
std::optional<CompilerMessage> messageOf(const QString& line) {
    if (line.startsWith(QLatin1String("$ "))) return std::nullopt;
    return parseCompilerMessage(line);
}

// Errors in red, warnings in amber, notes muted: the lines one can click.
class MessageHighlighter : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString& text) override {
        const auto message = messageOf(text);
        if (!message) return;
        QTextCharFormat format;
        format.setFontUnderline(true);
        switch (message->kind) {
        case CompilerMessage::Kind::Error:
            format.setForeground(QColor(0xc0, 0x10, 0x10));
            format.setFontWeight(QFont::Bold);
            break;
        case CompilerMessage::Kind::Warning: format.setForeground(QColor(0xa0, 0x60, 0x00)); break;
        case CompilerMessage::Kind::Note: format.setForeground(QColor(0x40, 0x60, 0x90)); break;
        }
        setFormat(0, int(text.size()), format);
    }
};

} // namespace

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
    new MessageHighlighter(m_output->document());
    // A click on a compiler's message opens its place.
    m_output->viewport()->installEventFilter(this);
    m_output->viewport()->setMouseTracking(true);

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
    m_workingDirectory = command.workingDirectory;
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
    // Looked at four times a second: a shorter build was never seen.
    if (m_peak > 0) summary += tr(" The build took at most %1 of memory.").arg(BuildMonitor::formatBytes(m_peak));
    int errors = 0;
    int warnings = 0;
    for (QTextBlock block = m_output->document()->begin(); block.isValid(); block = block.next()) {
        const auto message = messageOf(block.text());
        if (!message) continue;
        if (message->kind == CompilerMessage::Kind::Error) ++errors;
        else if (message->kind == CompilerMessage::Kind::Warning) ++warnings;
    }
    if (errors || warnings)
        summary += QLatin1Char(' ') + tr("%n error(s)", nullptr, errors) + QStringLiteral(", ")
                   + tr("%n warning(s)", nullptr, warnings) + tr("; click one to open its place.");
    m_output->appendPlainText(QStringLiteral("\n") + summary);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
    m_status->setText(QStringLiteral("<b>%1</b> — %2").arg(m_title.toHtmlEscaped(), summary.toHtmlEscaped()));
    m_stopButton->setEnabled(false);
    m_process->deleteLater();
    m_process = nullptr;
    emit finished(success);
}

QString BuildPanel::messageFileAt(const QPoint& position, int* line, int* column) const {
    const QTextBlock block = m_output->cursorForPosition(position).block();
    if (!block.isValid()) return {};
    // Only on the text of the line, not in the empty space after it.
    QTextCursor end(block);
    end.movePosition(QTextCursor::EndOfBlock);
    if (position.x() > m_output->cursorRect(end).right()) return {};
    const auto message = messageOf(block.text());
    if (!message) return {};
    *line = message->line;
    *column = message->column;
    return QDir::cleanPath(QDir(m_workingDirectory).absoluteFilePath(message->file));
}

bool BuildPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_output->viewport()) return QWidget::eventFilter(watched, event);
    int line = 0;
    int column = 0;
    if (event->type() == QEvent::MouseMove) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const bool overMessage =
            mouse->buttons() == Qt::NoButton && !messageFileAt(mouse->position().toPoint(), &line, &column).isEmpty();
        m_output->viewport()->setCursor(overMessage ? Qt::PointingHandCursor : Qt::IBeamCursor);
    } else if (event->type() == QEvent::MouseButtonRelease) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        // A click, not the end of selecting text.
        if (mouse->button() == Qt::LeftButton && !m_output->textCursor().hasSelection()) {
            const QString file = messageFileAt(mouse->position().toPoint(), &line, &column);
            if (!file.isEmpty()) emit locationActivated(file, line, column);
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString BuildPanel::duration(qint64 milliseconds) {
    const qint64 seconds = milliseconds / 1000;
    if (seconds < 60) return tr("%1 s").arg(double(milliseconds) / 1000, 0, 'f', 1);
    if (seconds < 3600) return tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60);
    return tr("%1 h %2 min").arg(seconds / 3600).arg(seconds % 3600 / 60);
}

} // namespace eudaimonia
