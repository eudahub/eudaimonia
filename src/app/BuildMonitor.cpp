// SPDX-License-Identifier: GPL-3.0-only
#include "BuildMonitor.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QList>
#include <QObject>

#include <algorithm>

#include <unistd.h>

namespace eudaimonia {
namespace {

constexpr qint64 kiB = 1024;
constexpr qint64 MiB = 1024 * kiB;
constexpr qint64 GiB = 1024 * MiB;

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

BuildMonitor::Memory BuildMonitor::parseMeminfo(const QByteArray& meminfo) {
    Memory memory;
    for (const QByteArray& line : meminfo.split('\n')) {
        // "MemAvailable:   12345678 kB"
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) continue;
        const QByteArray name = line.left(colon);
        const qint64 value = line.mid(colon + 1).trimmed().split(' ').first().toLongLong() * kiB;
        if (name == "MemTotal") memory.total = value;
        else if (name == "MemAvailable") memory.available = value;
        else if (name == "SwapTotal") memory.swapTotal = value;
        else if (name == "SwapFree") memory.swapFree = value;
    }
    return memory;
}

BuildMonitor::Memory BuildMonitor::readMemory(const QString& procRoot) {
    return parseMeminfo(readFile(procRoot + QStringLiteral("/meminfo")));
}

BuildMonitor::Limits BuildMonitor::defaultLimits(const Memory& memory) {
    return {std::max(2 * GiB, memory.total / 20), 256 * MiB};
}

QString BuildMonitor::stopReason(const Memory& now, const Memory& start, const Limits& limits) {
    if (now.total > 0 && now.available < limits.minAvailable)
        return QObject::tr("only %1 of memory left (the limit is %2)")
            .arg(formatBytes(now.available), formatBytes(limits.minAvailable));
    if (now.swapUsed() - start.swapUsed() > limits.maxSwapGrowth)
        return QObject::tr("swap in use grew by %1 during the build").arg(formatBytes(now.swapUsed() - start.swapUsed()));
    return {};
}

qint64 BuildMonitor::treeResident(qint64 pid, const QString& procRoot) {
    // Every process's parent and resident pages, from /proc/<pid>/stat:
    // "pid (comm) state ppid ... rss ..." — comm may hold spaces and
    // parentheses, so the fields are counted after its last ')'.
    QHash<qint64, QList<qint64>> children;
    QHash<qint64, qint64> resident;
    const QDir proc(procRoot);
    for (const QString& name : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool number = false;
        const qint64 process = name.toLongLong(&number);
        if (!number) continue;
        const QByteArray stat = readFile(proc.filePath(name + QStringLiteral("/stat")));
        const qsizetype end = stat.lastIndexOf(')');
        if (end < 0) continue;
        // After ")": state(3) ppid(4) ... rss(24); fields counted from 3.
        const QList<QByteArray> fields = stat.mid(end + 2).split(' ');
        if (fields.size() < 22) continue;
        children[fields.at(1).toLongLong()] << process;
        resident[process] = fields.at(21).toLongLong();
    }
    if (!resident.contains(pid)) return 0;
    static const qint64 pageSize = sysconf(_SC_PAGESIZE);
    qint64 pages = 0;
    QList<qint64> pending{pid};
    while (!pending.isEmpty()) {
        const qint64 process = pending.takeLast();
        pages += resident.value(process);
        pending << children.value(process);
    }
    return pages * pageSize;
}

QString BuildMonitor::formatBytes(qint64 bytes) {
    if (bytes >= GiB) return QStringLiteral("%1 GiB").arg(double(bytes) / GiB, 0, 'f', 1);
    return QStringLiteral("%1 MiB").arg(bytes / MiB);
}

} // namespace eudaimonia
