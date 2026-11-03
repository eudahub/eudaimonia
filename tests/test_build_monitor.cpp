// SPDX-License-Identifier: GPL-3.0-only
#include "BuildMonitor.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using eudaimonia::BuildMonitor;

namespace {

constexpr qint64 MiB = 1024 * 1024;
constexpr qint64 GiB = 1024 * MiB;

BuildMonitor::Memory memory(qint64 total, qint64 available, qint64 swapTotal = 0, qint64 swapFree = 0) {
    BuildMonitor::Memory result;
    result.total = total;
    result.available = available;
    result.swapTotal = swapTotal;
    result.swapFree = swapFree;
    return result;
}

} // namespace

class TestBuildMonitor : public QObject {
    Q_OBJECT

private slots:
    void parsesMeminfo() {
        const BuildMonitor::Memory parsed = BuildMonitor::parseMeminfo("MemTotal:       201326592 kB\n"
                                                                       "MemFree:         1000 kB\n"
                                                                       "MemAvailable:   104857600 kB\n"
                                                                       "SwapTotal:        2097152 kB\n"
                                                                       "SwapFree:         1048576 kB\n");
        QCOMPARE(parsed.total, 192 * GiB);
        QCOMPARE(parsed.available, 100 * GiB);
        QCOMPARE(parsed.swapUsed(), 1 * GiB);
        const BuildMonitor::Memory real = BuildMonitor::readMemory();
        QVERIFY(real.total > 0);
        QVERIFY(real.available > 0 && real.available <= real.total);
    }

    void limitsFollowTheMemory() {
        // 5 % of a large memory; at least 2 GiB of a small one.
        QCOMPARE(BuildMonitor::defaultLimits(memory(200 * GiB, 0)).minAvailable, 10 * GiB);
        QCOMPARE(BuildMonitor::defaultLimits(memory(20 * GiB, 0)).minAvailable, 2 * GiB);
        QCOMPARE(BuildMonitor::defaultLimits(memory(20 * GiB, 0)).maxSwapGrowth, 256 * MiB);
    }

    void stopsWhenMemoryRunsOut() {
        const BuildMonitor::Memory start = memory(20 * GiB, 15 * GiB, 2 * GiB, 2 * GiB);
        const BuildMonitor::Limits limits = BuildMonitor::defaultLimits(start);
        QVERIFY(BuildMonitor::stopReason(start, start, limits).isEmpty());
        QVERIFY(BuildMonitor::stopReason(memory(20 * GiB, 3 * GiB, 2 * GiB, 2 * GiB), start, limits).isEmpty());
        QVERIFY(!BuildMonitor::stopReason(memory(20 * GiB, 1 * GiB, 2 * GiB, 2 * GiB), start, limits).isEmpty());
        // Swap used before the build does not count; what the build adds does.
        const BuildMonitor::Memory swapping = memory(20 * GiB, 10 * GiB, 2 * GiB, 2 * GiB - 300 * MiB);
        QVERIFY(!BuildMonitor::stopReason(swapping, start, limits).isEmpty());
        QVERIFY(BuildMonitor::stopReason(swapping, swapping, limits).isEmpty());
    }

    // A /proc of its own: comm with spaces and parentheses, a tree and a stranger.
    void sumsTheTreeOfProcesses() {
        QTemporaryDir proc;
        auto stat = [&](int pid, const QByteArray& comm, int ppid, int rss) {
            QDir(proc.path()).mkpath(QString::number(pid));
            QFile file(proc.filePath(QStringLiteral("%1/stat").arg(pid)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QByteArray fields = QByteArray::number(pid) + " (" + comm + ") S " + QByteArray::number(ppid);
            for (int field = 5; field < 24; ++field) fields += " 0";
            fields += ' ' + QByteArray::number(rss) + " 0 0\n";
            file.write(fields);
        };
        stat(100, "ninja", 1, 10);
        stat(101, "cc1plus (a) b", 100, 5);
        stat(102, "ld", 101, 2);
        stat(200, "other", 1, 50);
        const qint64 page = sysconf(_SC_PAGESIZE);
        QCOMPARE(BuildMonitor::treeResident(100, proc.path()), 17 * page);
        QCOMPARE(BuildMonitor::treeResident(101, proc.path()), 7 * page);
        QCOMPARE(BuildMonitor::treeResident(300, proc.path()), 0);
    }

    // Two packages, the first with SMT: core numbers repeat across packages.
    void countsPhysicalCores() {
        QTemporaryDir root;
        auto cpu = [&](int number, int package, int core) {
            const QString topology = QStringLiteral("cpu%1/topology/").arg(number);
            QDir(root.path()).mkpath(topology);
            QFile packageFile(root.filePath(topology + QStringLiteral("physical_package_id")));
            QVERIFY(packageFile.open(QIODevice::WriteOnly));
            packageFile.write(QByteArray::number(package) + '\n');
            QFile coreFile(root.filePath(topology + QStringLiteral("core_id")));
            QVERIFY(coreFile.open(QIODevice::WriteOnly));
            coreFile.write(QByteArray::number(core) + '\n');
        };
        cpu(0, 0, 0);
        cpu(1, 0, 1);
        cpu(2, 0, 0);
        cpu(3, 0, 1);
        cpu(4, 1, 0);
        QDir(root.path()).mkpath(QStringLiteral("cpufreq"));    // not a processor
        QDir(root.path()).mkpath(QStringLiteral("cpu5"));       // offline: no topology
        QCOMPARE(BuildMonitor::physicalCores(root.path()), 3);
        // Unreadable: the logical processors, not nothing.
        QTemporaryDir empty;
        QVERIFY(BuildMonitor::physicalCores(empty.path()) > 0);
        QVERIFY(BuildMonitor::physicalCores() > 0);
    }

    void measuresARealTree() {
        QProcess shell;
        shell.start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("sleep 5 & sleep 5 & wait")});
        QVERIFY(shell.waitForStarted());
        QTest::qWait(200);
        const qint64 tree = BuildMonitor::treeResident(shell.processId());
        QVERIFY(tree > 0);
        shell.kill();
        shell.waitForFinished();
    }

    void formatsSizes() {
        QCOMPARE(BuildMonitor::formatBytes(230 * MiB), QStringLiteral("230 MiB"));
        QCOMPARE(BuildMonitor::formatBytes(3 * GiB / 2), QStringLiteral("1.5 GiB"));
    }
};

QTEST_GUILESS_MAIN(TestBuildMonitor)
#include "test_build_monitor.moc"
