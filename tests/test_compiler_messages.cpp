// SPDX-License-Identifier: GPL-3.0-only
#include "CompilerMessages.h"

#include <QTest>

using eudaimonia::CompilerMessage;
using eudaimonia::parseCompilerMessage;
using Kind = eudaimonia::CompilerMessage::Kind;

Q_DECLARE_METATYPE(Kind)

class TestCompilerMessages : public QObject {
    Q_OBJECT

private slots:
    void recognizes_data() {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("lineNumber");
        QTest::addColumn<int>("column");
        QTest::addColumn<Kind>("kind");
        QTest::newRow("gcc error") << "/home/u/p/src/main.cpp:12:5: error: 'x' was not declared in this scope"
                                   << "/home/u/p/src/main.cpp" << 12 << 5 << Kind::Error;
        QTest::newRow("clang warning") << "src/a.cpp:3:10: warning: unused variable 'y' [-Wunused-variable]"
                                       << "src/a.cpp" << 3 << 10 << Kind::Warning;
        QTest::newRow("fatal error") << "a.cpp:1:10: fatal error: missing.h: No such file or directory"
                                     << "a.cpp" << 1 << 10 << Kind::Error;
        QTest::newRow("note") << "a.h:7:6: note: declared here" << "a.h" << 7 << 6 << Kind::Note;
        QTest::newRow("no column") << "a.cpp:42: warning: something" << "a.cpp" << 42 << 0 << Kind::Warning;
        QTest::newRow("included from") << "In file included from /p/b.h:3," << "/p/b.h" << 3 << 0 << Kind::Note;
        QTest::newRow("from") << "                 from /p/main.cpp:1:" << "/p/main.cpp" << 1 << 0 << Kind::Note;
        QTest::newRow("linker") << "/usr/bin/ld: /p/main.cpp:5:(.text+0x9): undefined reference to `f()'"
                                << "/p/main.cpp" << 5 << 0 << Kind::Error;
        QTest::newRow("spaces in path") << "/p/my dir/a.cpp:2:1: error: expected ';'" << "/p/my dir/a.cpp" << 2 << 1
                                        << Kind::Error;
    }

    void recognizes() {
        QFETCH(QString, line);
        const auto message = parseCompilerMessage(line);
        QVERIFY(message);
        QTEST(message->file, "file");
        QTEST(message->line, "lineNumber");
        QTEST(message->column, "column");
        QTEST(message->kind, "kind");
    }

    // rustc tells the kind on the line before the place.
    void recognizesRustc_data() {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("previous");
        QTest::addColumn<int>("column");
        QTest::addColumn<Kind>("kind");
        QTest::newRow("error") << " --> src/main.rs:2:18" << "error[E0308]: mismatched types" << 18 << Kind::Error;
        QTest::newRow("warning") << "  --> src/main.rs:2:18" << "warning: unused variable: `x`" << 18 << Kind::Warning;
        QTest::newRow("other place") << "  ::: src/main.rs:2:3" << "   |" << 3 << Kind::Note;
    }

    void recognizesRustc() {
        QFETCH(QString, line);
        QFETCH(QString, previous);
        const auto message = parseCompilerMessage(line, previous);
        QVERIFY(message);
        QCOMPARE(message->file, QStringLiteral("src/main.rs"));
        QCOMPARE(message->line, 2);
        QTEST(message->column, "column");
        QTEST(message->kind, "kind");
    }

    void ignoresOtherLines_data() {
        QTest::addColumn<QString>("line");
        QTest::newRow("ninja") << "[3/10] Building CXX object CMakeFiles/x.dir/main.cpp.o";
        QTest::newRow("failed") << "FAILED: CMakeFiles/x.dir/main.cpp.o";
        QTest::newRow("in function") << "/p/main.cpp: In function 'int main()':";
        QTest::newRow("source excerpt") << "   12 |     int x = y;";
        QTest::newRow("linker object") << "/usr/bin/ld: main.o: in function `main':";
        QTest::newRow("summary") << "Built in 1.2 s, finished at 14:05:12.";
        QTest::newRow("rustc's message") << "error[E0308]: mismatched types";
        QTest::newRow("rustc's excerpt") << "2 |     let x: i32 = \"a\";";
        QTest::newRow("empty") << "";
    }

    void ignoresOtherLines() {
        QFETCH(QString, line);
        QVERIFY(!parseCompilerMessage(line));
    }
};

QTEST_GUILESS_MAIN(TestCompilerMessages)
#include "test_compiler_messages.moc"
