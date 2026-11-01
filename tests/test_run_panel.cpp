// SPDX-License-Identifier: GPL-3.0-only
// The run panel on real processes (offscreen): output and errors, input
// lines, end of input, the exit status, and stopping the program with what it
// started.
#include "RunPanel.h"

#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include <csignal>

#include <sys/types.h>

using eudaimonia::RunPanel;

namespace {

const QString sh = QStringLiteral("sh");

bool alive(pid_t pid) {
    return pid > 0 && ::kill(pid, 0) == 0;
}

} // namespace

class TestRunPanel : public QObject {
    Q_OBJECT

private slots:
    void showsOutputErrorsAndTheExitCode() {
        RunPanel panel;
        QSignalSpy finished(&panel, &RunPanel::finished);
        QVERIFY(panel.start(sh, {QStringLiteral("-c"), QStringLiteral("echo out; echo err >&2; exit 4")}, QString()));
        QVERIFY(panel.isRunning());
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().at(0).toInt(), 4);
        QVERIFY(panel.output().contains(QStringLiteral("out")));
        QVERIFY(panel.output().contains(QStringLiteral("err")));
        QVERIFY(panel.output().contains(QStringLiteral("exited with code 4")));
    }

    void takesInputLines() {
        RunPanel panel;
        QSignalSpy finished(&panel, &RunPanel::finished);
        panel.start(sh, {QStringLiteral("-c"), QStringLiteral("read line; echo got:$line; cat; echo done")}, QString());
        auto* input = panel.findChild<QLineEdit*>();
        QVERIFY(input && input->isEnabled());
        input->setText(QStringLiteral("hello"));
        QTest::keyClick(input, Qt::Key_Return);
        QTRY_VERIFY(panel.output().contains(QStringLiteral("got:hello")));
        // cat waits for the end of input.
        QToolButton* end = nullptr;
        for (auto* button : panel.findChildren<QToolButton*>())
            if (button->text() == QStringLiteral("End Input")) end = button;
        QVERIFY(end);
        end->click();
        QVERIFY(finished.wait(10000));
        QVERIFY(panel.output().contains(QStringLiteral("done")));
    }

    void stopsTheProgramWithWhatItStarted() {
        RunPanel panel;
        QSignalSpy finished(&panel, &RunPanel::finished);
        panel.start(sh, {QStringLiteral("-c"), QStringLiteral("sleep 60 & echo child=$!; wait")}, QString());
        QTRY_VERIFY(panel.output().contains(QStringLiteral("child=")));
        const pid_t child = panel.output().section(QStringLiteral("child="), 1).section(QLatin1Char('\n'), 0, 0).toInt();
        QVERIFY(alive(child));
        panel.stop();
        QVERIFY(finished.wait(10000));
        QVERIFY(panel.output().contains(QStringLiteral("stopped after")));
        QTRY_VERIFY(!alive(child));
    }
};

QTEST_MAIN(TestRunPanel)
#include "test_run_panel.moc"
