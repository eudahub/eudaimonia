// SPDX-License-Identifier: GPL-3.0-only
#include "TestInterface.h"

#include <QObject>

// Built twice, as plugins "first" and "second" with the same IID.
class TestPlugin : public QObject, public TestInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TestInterface_iid)
    Q_INTERFACES(TestInterface)

public:
    QString name() const override { return QStringLiteral(TEST_PLUGIN_NAME); }
};

#include "TestPlugin.moc"
