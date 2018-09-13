#include "wizardchirpconfigpage.h"

#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QSpinBox>

#include "chirpconfigwidget.h"
#include "experimentwizard.h"

WizardChirpConfigPage::WizardChirpConfigPage(QWidget *parent) :
    ExperimentWizardPage(parent)
{
    setTitle(QString("Configure FTMW Chirp"));

    p_ccw = new ChirpConfigWidget(this);
    connect(p_ccw,&ChirpConfigWidget::chirpConfigChanged,this,&WizardChirpConfigPage::completeChanged);
    registerField(QString("numChirps"),p_ccw->numChirpsBox());

    QVBoxLayout *vbl = new QVBoxLayout(this);
    vbl->addWidget(p_ccw);

    setLayout(vbl);

}

WizardChirpConfigPage::~WizardChirpConfigPage()
{

}

void WizardChirpConfigPage::initializePage()
{
    //get rfConfig
    auto e = getExperiment();
    p_ccw->setRfConfig(e.ftmwConfig().rfConfig());
    p_ccw->updateChirpPlot();

}


int WizardChirpConfigPage::nextId() const
{
    return ExperimentWizard::FtmwConfigPage;
}


bool WizardChirpConfigPage::validatePage()
{
    ///TODO: Smarter validation?
    auto e = getExperiment();
    auto rfc = p_ccw->getRfConfig();
    e.setRfConfig(rfc);
    emit experimentUpdate(e);
    return true;
}
