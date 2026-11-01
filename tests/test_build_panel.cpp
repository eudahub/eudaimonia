// SPDX-License-Identifier: GPL-3.0-only
// The build panel on real processes (offscreen): the output and the summary,
// stopping by the user and for lack of memory, with every process of the
// build gone afterwards.
#include "BuildPanel.h"

#include <QDir>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>

#include <csignal>

#include <sys/types.h>

using eudaimonia::BuildCommand;
using eudaimonia::BuildMonitor;
using eudaimonia::BuildPanel;

namespace {

// A build whose child prints its pid and waits long.
BuildCommand longBuild() {
    return {QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("sleep 60 & echo child=$!; wait")}, QString()};
}

// The pid the child printed (the panel shows the command, with "child=", first).
pid_t childOf(const QString& output) {
    const qsizetype at = output.lastIndexOf(QStringLiteral("child="));
    return at < 0 ? 0 : output.mid(at + 6).section(QLatin1Char('\n'), 0, 0).toInt();
}

bool alive(pid_t pid) {
    return pid > 0 && ::kill(pid, 0) == 0;
}

} // namespace

class TestBuildPanel : public QObject {
    Q_OBJECT

private slots:
    void showsTheOutputAndTheSummary() {
        BuildPanel panel;
        QSignalSpy finished(&panel, &BuildPanel::finished);
        QVERIFY(panel.start({QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("echo compiling; exit 0")}, {}},
                            QStringLiteral("Build test")));
        QVERIFY(panel.isRunning());
        QVERIFY(!panel.start(longBuild(), QStringLiteral("another")));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().first().toBool(), true);
        QVERIFY(panel.output().contains(QStringLiteral("compiling")));
        QVERIFY(panel.output().contains(QStringLiteral("Built in ")));
        QVERIFY(panel.output().contains(QStringLiteral("finished at ")));
        QVERIFY(!panel.isRunning());
    }

    void reportsAFailure() {
        BuildPanel panel;
        QSignalSpy finished(&panel, &BuildPanel::finished);
        panel.start({QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("exit 3")}, {}}, QStringLiteral("Build"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().first().toBool(), false);
        QVERIFY(panel.output().contains(QStringLiteral("exit code 3")));
    }

    void stopsTheWholeBuildWhenAsked() {
        BuildPanel panel;
        QSignalSpy finished(&panel, &BuildPanel::finished);
        panel.start(longBuild(), QStringLiteral("Build"));
        QTRY_VERIFY(childOf(panel.output()) > 0);
        const pid_t child = childOf(panel.output());
        QVERIFY(alive(child));
        panel.stop();
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().first().toBool(), false);
        QVERIFY(panel.output().contains(QStringLiteral("stopped by the user")));
        QTRY_VERIFY(!alive(child));
    }

    // A compiler's error: counted in the summary, and a click on it gives
    // its place, a relative path resolved in the build's directory.
    void opensTheMessagesPlace() {
        BuildPanel panel;
        panel.resize(900, 400);
        panel.show();
        QSignalSpy finished(&panel, &BuildPanel::finished);
        QSignalSpy activated(&panel, &BuildPanel::locationActivated);
        QTemporaryDir project;
        QDir(project.path()).mkpath(QStringLiteral("build"));
        panel.start({QStringLiteral("sh"),
                     {QStringLiteral("-c"),
                      QStringLiteral("echo '../src/main.cpp:12:5: error: x was not declared'; "
                                     "echo 'a.h:3:1: warning: unused'; exit 1")},
                     project.filePath(QStringLiteral("build"))},
                    QStringLiteral("Build"));
        QVERIFY(finished.wait(10000));
        QVERIFY(panel.output().contains(QStringLiteral("1 error(s), 1 warning(s)")));

        auto* output = panel.findChild<QPlainTextEdit*>();
        QVERIFY(output);
        QTextBlock block = output->document()->begin();
        while (block.isValid() && !block.text().startsWith(QStringLiteral("../src/main.cpp"))) block = block.next();
        QVERIFY(block.isValid());
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 3);
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        QTest::mouseClick(output->viewport(), Qt::LeftButton, {}, output->cursorRect(cursor).center());
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().at(0).toString(), project.filePath(QStringLiteral("src/main.cpp")));
        QCOMPARE(activated.first().at(1).toInt(), 12);
        QCOMPARE(activated.first().at(2).toInt(), 5);

        // The summary line is no message: nothing happens.
        QTextCursor last(output->document()->lastBlock());
        QTest::mouseClick(output->viewport(), Qt::LeftButton, {}, output->cursorRect(last).center() + QPoint(2, 0));
        QCOMPARE(activated.count(), 1);
    }

    // A limit no computer meets: the build is killed at the first look.
    void stopsTheBuildForLackOfMemory() {
        BuildPanel panel;
        BuildMonitor::Limits limits;
        limits.minAvailable = BuildMonitor::readMemory().total + 1;
        limits.maxSwapGrowth = 256 * 1024 * 1024;
        panel.setLimits(limits);
        QSignalSpy finished(&panel, &BuildPanel::finished);
        panel.start(longBuild(), QStringLiteral("Build"));
        QTRY_VERIFY(childOf(panel.output()) > 0);
        const pid_t child = childOf(panel.output());
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().first().toBool(), false);
        QVERIFY(panel.output().contains(QStringLiteral("for lack of memory")));
        QTRY_VERIFY(!alive(child));
    }
};

QTEST_MAIN(TestBuildPanel)
#include "test_build_panel.moc"
