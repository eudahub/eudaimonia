// SPDX-License-Identifier: GPL-3.0-only
// The build panel on real processes (offscreen): the output and the summary,
// stopping by the user and for lack of memory, with every process of the
// build gone afterwards.
#include "BuildPanel.h"

#include <QSignalSpy>
#include <QTest>

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
