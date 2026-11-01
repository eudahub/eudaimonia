// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QString>

namespace eudaimonia {

// Watches the memory of a build (Linux, /proc): what its processes take and
// what the computer has left, so that a build stops before the computer runs
// out of memory or starts swapping — a swapping build makes the computer
// useless for long and wears the disk.
class BuildMonitor {
public:
    // The computer's memory, in bytes (from /proc/meminfo).
    struct Memory {
        qint64 total = 0;
        qint64 available = 0;  // MemAvailable: what can be used without swapping
        qint64 swapTotal = 0;
        qint64 swapFree = 0;
        qint64 swapUsed() const { return swapTotal - swapFree; }
    };

    // When a build is stopped.
    struct Limits {
        qint64 minAvailable = 0;  // less available memory than this
        qint64 maxSwapGrowth = 0; // swap in use grown by more than this since the start
    };

    static Memory parseMeminfo(const QByteArray& meminfo);
    // `procRoot` is /proc but for tests.
    static Memory readMemory(const QString& procRoot = QStringLiteral("/proc"));

    // 5 % of the memory, at least 2 GiB; swap grown by 256 MiB.
    static Limits defaultLimits(const Memory& memory);

    // Why the build must stop now, or empty: `start` is the memory when it
    // began.
    static QString stopReason(const Memory& now, const Memory& start, const Limits& limits);

    // The resident memory of the process `pid` and of all its descendants
    // (a build's compilers and linkers); 0 when it is gone.
    static qint64 treeResident(qint64 pid, const QString& procRoot = QStringLiteral("/proc"));

    // "1.5 GiB", "230 MiB".
    static QString formatBytes(qint64 bytes);
};

} // namespace eudaimonia
