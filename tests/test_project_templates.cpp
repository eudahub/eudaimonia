// SPDX-License-Identifier: GPL-3.0-only
// The templates of new projects, from the program's resources: every one makes
// its files with the name filled in; the directory must not exist yet.
#include "ProjectTemplates.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace eudaimonia;

namespace {

QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

const ProjectTemplate* templateNamed(const QString& id) {
    for (const ProjectTemplate& one : projectTemplates())
        if (one.id == id) return &one;
    return nullptr;
}

} // namespace

class TestProjectTemplates : public QObject {
    Q_OBJECT

private slots:
    void acceptsNamesFitForEveryKind_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("valid");
        QTest::newRow("plain") << "app" << true;
        QTest::newRow("all kinds") << "My-app_2" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("digit first") << "1app" << false;
        QTest::newRow("dash first") << "-app" << false;
        QTest::newRow("space") << "my app" << false;
        QTest::newRow("slash") << "a/b" << false;
        QTest::newRow("dots") << ".." << false;
        QTest::newRow("not ascii") << QStringLiteral("zażółć") << false;
    }

    void acceptsNamesFitForEveryKind() {
        QFETCH(QString, name);
        QTEST(isValidProjectName(name), "valid");
    }

    void everyTemplateMakesItsFiles() {
        QTemporaryDir root;
        QCOMPARE(projectTemplates().size(), 9);
        for (const ProjectTemplate& one : projectTemplates()) {
            const QString directory = root.filePath(one.id + QStringLiteral("/My-app"));
            QString error;
            QVERIFY2(createProject(one, directory, QStringLiteral("My-app"), &error), qPrintable(error));
            QVERIFY2(QFile::exists(QDir(directory).filePath(one.mainFile)), qPrintable(one.id));
            QVERIFY2(QFile::exists(QDir(directory).filePath(QStringLiteral(".gitignore"))), qPrintable(one.id));
            QDirIterator files(directory, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                const QString file = files.next();
                QVERIFY2(!file.endsWith(QLatin1String(".in")), qPrintable(file));
                // Nothing is left to fill in.
                QVERIFY2(!read(file).contains('@'), qPrintable(file));
            }
        }
    }

    // A file of a template not in the list of resources would go missing unseen.
    void eachKnownTemplateHasResources() {
        QStringList directories = QDir(QStringLiteral(":/templates")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        directories.sort();
        QStringList ids;
        for (const ProjectTemplate& one : projectTemplates()) ids << one.id;
        ids.sort();
        QCOMPARE(directories, ids);
        // Every file of the sources' directory is in the program.
        QDirIterator sources(QStringLiteral(TEMPLATES_DIR), QDir::Files, QDirIterator::Subdirectories);
        int count = 0;
        while (sources.hasNext()) {
            const QString relative = QDir(QStringLiteral(TEMPLATES_DIR)).relativeFilePath(sources.next());
            QVERIFY2(QFile::exists(QStringLiteral(":/templates/") + relative), qPrintable(relative));
            ++count;
        }
        QVERIFY(count > 0);
    }

    void fillsInTheName() {
        QTemporaryDir root;
        QString error;
        const QString rust = root.filePath(QStringLiteral("rust"));
        QVERIFY(createProject(*templateNamed(QStringLiteral("rust-program")), rust, QStringLiteral("My-app"), &error));
        QVERIFY(read(rust + QStringLiteral("/Cargo.toml")).contains("name = \"My-app\""));
        QVERIFY(read(rust + QStringLiteral("/src/main.rs")).contains("Hello from My-app!"));
        // npm wants lower case.
        const QString javascript = root.filePath(QStringLiteral("javascript"));
        QVERIFY(createProject(*templateNamed(QStringLiteral("javascript")), javascript, QStringLiteral("My-app"), &error));
        QVERIFY(read(javascript + QStringLiteral("/package.json")).contains("\"name\": \"my-app\""));
    }

    void onlyInADirectoryThatDoesNotExist() {
        QTemporaryDir root;
        const QString directory = root.filePath(QStringLiteral("taken"));
        QVERIFY(QDir().mkpath(directory));
        QFile mine(directory + QStringLiteral("/mine.txt"));
        QVERIFY(mine.open(QIODevice::WriteOnly));
        mine.write("mine");
        mine.close();
        QString error;
        QVERIFY(!createProject(projectTemplates().first(), directory, QStringLiteral("taken"), &error));
        QVERIFY(error.contains(directory));
        // Nothing was written there.
        QCOMPARE(QDir(directory).entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("mine.txt")});
        // What leads to a new one is made.
        const QString deep = root.filePath(QStringLiteral("a/b/new"));
        QVERIFY2(createProject(projectTemplates().first(), deep, QStringLiteral("new"), &error), qPrintable(error));
        QVERIFY(QFile::exists(deep + QStringLiteral("/main.cpp")));
    }
};

QTEST_GUILESS_MAIN(TestProjectTemplates)
#include "test_project_templates.moc"
