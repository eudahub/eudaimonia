// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ProjectTemplates.h"

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;

namespace eudaimonia {

// Asks for a kind of project, its name and where it goes. A project can only
// be made in a directory that does not exist yet: it says so otherwise, and OK
// stays off.
class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(const QString& location, QWidget* parent = nullptr);

    const ProjectTemplate& chosenTemplate() const;
    QString name() const;
    QString location() const;
    // The new project's directory: the location and the name.
    QString directory() const;

private:
    void update();
    void browse();

    QListWidget* m_templates = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_location = nullptr;
    QLabel* m_description = nullptr;
    QLabel* m_result = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

} // namespace eudaimonia
