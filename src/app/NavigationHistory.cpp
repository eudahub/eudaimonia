// SPDX-License-Identifier: GPL-3.0-only
#include "NavigationHistory.h"

namespace eudaimonia {
namespace {

bool sameLine(const NavigationHistory::Place& a, const NavigationHistory::Place& b) {
    return a.file == b.file && a.line == b.line;
}

void push(QList<NavigationHistory::Place>& places, const NavigationHistory::Place& place) {
    if (place.file.isEmpty()) return;
    if (!places.isEmpty() && sameLine(places.last(), place)) {
        places.last() = place;
        return;
    }
    places << place;
    if (places.size() > NavigationHistory::maxPlaces) places.removeFirst();
}

} // namespace

void NavigationHistory::leave(const Place& from) {
    push(m_back, from);
    m_forward.clear();
}

NavigationHistory::Place NavigationHistory::back(const Place& current) {
    if (m_back.isEmpty()) return current;
    // Back from where one stands: that place is the way forward.
    Place target = m_back.takeLast();
    if (sameLine(target, current) && !m_back.isEmpty()) target = m_back.takeLast();
    push(m_forward, current);
    return target;
}

NavigationHistory::Place NavigationHistory::forward(const Place& current) {
    if (m_forward.isEmpty()) return current;
    Place target = m_forward.takeLast();
    push(m_back, current);
    return target;
}

void NavigationHistory::clear() {
    m_back.clear();
    m_forward.clear();
}

} // namespace eudaimonia
