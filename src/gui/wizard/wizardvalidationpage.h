#ifndef WIZARDVALIDATIONPAGE_H
#define WIZARDVALIDATIONPAGE_H

#include "experimentwizardpage.h"

#include <QTableView>

#include "ioboardconfig.h"

class QToolButton;

class WizardValidationPage : public ExperimentWizardPage
{
public:
    explicit WizardValidationPage(QWidget *parent = nullptr);

private:
    QTableView *p_analogView, *p_digitalView, *p_validationView;
    QToolButton *p_addButton, *p_removeButton;

    // QWizardPage interface
public:
    int nextId() const;
    virtual void initializePage();
    virtual bool validatePage();

    IOBoardConfig getConfig() const;
    QMap<QString,BlackChirp::ValidationItem> getValidation() const;

};

#endif // WIZARDVALIDATIONPAGE_H
