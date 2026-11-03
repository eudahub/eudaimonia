// SPDX-License-Identifier: GPL-3.0-only
#include "NewProjectDialog.h"

#include <qxfiledialog.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace eudaimonia {

NewProjectDialog::NewProjectDialog(const QString& location, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("New Project"));
    m_templates = new QListWidget;
    for (const ProjectTemplate& projectTemplate : projectTemplates()) m_templates->addItem(projectTemplate.title);
    m_templates->setCurrentRow(0);
    connect(m_templates, &QListWidget::currentRowChanged, this, &NewProjectDialog::update);

    m_description = new QLabel;
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("A letter, then letters, digits, _ and -"));
    connect(m_name, &QLineEdit::textChanged, this, &NewProjectDialog::update);
    m_location = new QLineEdit(location);
    connect(m_location, &QLineEdit::textChanged, this, &NewProjectDialog::update);
    auto* browseButton = new QPushButton(tr("Browse..."));
    connect(browseButton, &QPushButton::clicked, this, &NewProjectDialog::browse);
    auto* locationRow = new QHBoxLayout;
    locationRow->addWidget(m_location, 1);
    locationRow->addWidget(browseButton);
    auto* form = new QFormLayout;
    form->addRow(tr("&Name:"), m_name);
    form->addRow(tr("Location:"), locationRow);

    m_result = new QLabel;
    m_result->setTextFormat(Qt::PlainText);
    m_result->setWordWrap(true);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_templates, 1);
    layout->addWidget(m_description);
    layout->addLayout(form);
    layout->addWidget(m_result);
    layout->addWidget(m_buttons);
    resize(560, 460);
    m_name->setFocus();
    update();
}

const ProjectTemplate& NewProjectDialog::chosenTemplate() const {
    return projectTemplates().at(std::max(0, m_templates->currentRow()));
}

QString NewProjectDialog::name() const {
    return m_name->text().trimmed();
}

QString NewProjectDialog::location() const {
    return QxFileDialog::expandHomePath(m_location->text().trimmed());
}

QString NewProjectDialog::directory() const {
    return QDir::cleanPath(QDir(location()).filePath(name()));
}

void NewProjectDialog::update() {
    m_description->setText(chosenTemplate().description);
    QString problem;
    if (name().isEmpty()) problem = tr("Give the project a name.");
    else if (!isValidProjectName(name())) problem = tr("A name is a letter, then letters, digits, _ and -.");
    else if (!QFileInfo(location()).isAbsolute()) problem = tr("The location must be a full path.");
    else if (QFileInfo::exists(directory())) problem = tr("%1 exists already: use Open Project Directory for it.").arg(directory());
    m_result->setText(problem.isEmpty() ? tr("Creates %1").arg(directory()) : problem);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

void NewProjectDialog::browse() {
    const QString chosen = QxFileDialog::getExistingDirectory(this, tr("Location of the New Project"), location());
    if (!chosen.isEmpty()) m_location->setText(chosen);
}

} // namespace eudaimonia
