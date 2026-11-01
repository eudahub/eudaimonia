// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QtPlugin>

// An interface only for the plugin manager's test.
class TestInterface {
public:
    virtual ~TestInterface() = default;
    virtual QString name() const = 0;
};

#define TestInterface_iid "io.github.eudahub.eudaimonia.Test/1"
Q_DECLARE_INTERFACE(TestInterface, TestInterface_iid)
