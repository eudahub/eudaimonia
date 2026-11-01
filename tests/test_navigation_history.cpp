// SPDX-License-Identifier: GPL-3.0-only
#include "NavigationHistory.h"

#include <QTest>

using eudaimonia::NavigationHistory;
using Place = eudaimonia::NavigationHistory::Place;

class TestNavigationHistory : public QObject {
    Q_OBJECT

private slots:
    void goesBackAndForward() {
        NavigationHistory history;
        QVERIFY(!history.canGoBack());
        const Place a{"a.cpp", 10, 2};
        const Place b{"b.cpp", 3, 0};
        const Place c{"a.cpp", 50, 4};
        history.leave(a);  // jumped from a to b
        history.leave(b);  // jumped from b to c
        QVERIFY(history.canGoBack());
        QCOMPARE(history.back(c), b);
        QCOMPARE(history.back(b), a);
        QVERIFY(!history.canGoBack());
        QCOMPARE(history.forward(a), b);
        QCOMPARE(history.forward(b), c);
        QVERIFY(!history.canGoForward());
    }

    void aNewJumpForgetsTheWayForward() {
        NavigationHistory history;
        history.leave({"a.cpp", 1, 0});
        QCOMPARE(history.back({"b.cpp", 2, 0}).file, QStringLiteral("a.cpp"));
        QVERIFY(history.canGoForward());
        history.leave({"a.cpp", 5, 0});
        QVERIFY(!history.canGoForward());
    }

    void keepsOnePlaceALine() {
        NavigationHistory history;
        history.leave({"a.cpp", 7, 1});
        history.leave({"a.cpp", 7, 9});
        QCOMPARE(history.back({"b.cpp", 0, 0}), Place({"a.cpp", 7, 9}));
        QVERIFY(!history.canGoBack());
    }

    // Back from the very place last left goes one further.
    void skipsTheCurrentPlace() {
        NavigationHistory history;
        history.leave({"a.cpp", 1, 0});
        history.leave({"b.cpp", 2, 0});
        QCOMPARE(history.back({"b.cpp", 2, 0}), Place({"a.cpp", 1, 0}));
    }

    void keepsAtMostAHundred() {
        NavigationHistory history;
        for (int line = 0; line < 150; ++line) history.leave({"a.cpp", line, 0});
        int steps = 0;
        Place current{"b.cpp", 0, 0};
        while (history.canGoBack()) {
            current = history.back(current);
            ++steps;
        }
        QCOMPARE(steps, NavigationHistory::maxPlaces);
        QCOMPARE(current.line, 50);
    }
};

QTEST_GUILESS_MAIN(TestNavigationHistory)
#include "test_navigation_history.moc"
