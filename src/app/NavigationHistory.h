// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>

namespace eudaimonia {

// Where one has been, across files: Back returns to the place before the
// last jump, Forward undoes a Back. A new jump after going back forgets the
// places ahead, as in a web browser.
class NavigationHistory {
public:
    struct Place {
        QString file;
        int line = 0;    // from 0
        int column = 0;  // from 0
        bool operator==(const Place& other) const = default;
    };

    // Before a jump: `from` is where one leaves. Places on the same line of
    // the same file as the last one are not kept twice.
    void leave(const Place& from);
    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    // The place to go to; `current` becomes the way forward (or back).
    Place back(const Place& current);
    Place forward(const Place& current);
    // Places in files that are gone, e.g. after the project changed.
    void clear();

    static constexpr int maxPlaces = 100;

private:
    QList<Place> m_back;
    QList<Place> m_forward;
};

} // namespace eudaimonia
