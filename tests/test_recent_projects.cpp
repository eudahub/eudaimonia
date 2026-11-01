// SPDX-License-Identifier: GPL-3.0-only
#include "RecentProjects.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using eudaimonia::RecentProjects;

class TestRecentProjects : public QObject {
    Q_OBJECT

private:
    QString makeDirectory(const QTemporaryDir& root, const QString& name) {
        QDir(root.path()).mkpath(name);
        return QDir(root.path()).filePath(name);
    }

    void writeFile(const QString& path, const QByteArray& bytes) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
    }

private slots:
    void newestFirstWithoutDuplicates() {
        QTemporaryDir root;
        const QString list = root.filePath("recent-projects.json");
        const QString a = makeDirectory(root, "a");
        const QString b = makeDirectory(root, "b");
        RecentProjects recent(list);
        QVERIFY(recent.remember(a));
        QVERIFY(recent.remember(b));
        QVERIFY(recent.remember(a + "/../a/"));
        QCOMPARE(recent.paths(), QStringList({a, b}));
    }

    void savedAndLoaded() {
        QTemporaryDir root;
        const QString list = root.filePath("data/recent-projects.json");
        const QString a = makeDirectory(root, "a");
        const QString b = makeDirectory(root, "b");
        {
            RecentProjects recent(list);
            QVERIFY(recent.remember(a));
            QVERIFY(recent.remember(b));
        }
        QCOMPARE(RecentProjects(list).paths(), QStringList({b, a}));
    }

    void keepsAtMostTwenty() {
        QTemporaryDir root;
        RecentProjects recent(root.filePath("recent-projects.json"));
        for (int i = 0; i < 25; ++i) QVERIFY(recent.remember(makeDirectory(root, QString::number(i))));
        QCOMPARE(recent.paths().size(), 20);
        QCOMPARE(recent.paths().first(), root.filePath("24"));
        QCOMPARE(recent.paths().last(), root.filePath("5"));
    }

    void malformedFileGivesEmptyList() {
        QTemporaryDir root;
        const QString list = root.filePath("recent-projects.json");
        writeFile(list, "{ not json");
        QVERIFY(RecentProjects(list).paths().isEmpty());
        writeFile(list, R"({"version": 2, "directories": ["/tmp"]})");
        QVERIFY(RecentProjects(list).paths().isEmpty());
    }

    void skipsEntriesThatAreNotStrings() {
        QTemporaryDir root;
        const QString list = root.filePath("recent-projects.json");
        const QString a = makeDirectory(root, "a");
        writeFile(list, QStringLiteral(R"({"version": 1, "directories": [1, "%1", null, "%1"]})").arg(a).toUtf8());
        QCOMPARE(RecentProjects(list).paths(), QStringList({a}));
    }
};

QTEST_GUILESS_MAIN(TestRecentProjects)
#include "test_recent_projects.moc"
