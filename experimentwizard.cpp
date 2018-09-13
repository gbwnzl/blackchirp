#include "experimentwizard.h"

#include "experimentwizardpage.h"
#include "wizardstartpage.h"
#include "wizardrfconfigpage.h"
#include "wizardchirpconfigpage.h"
#include "wizardftmwconfigpage.h"
#include "wizardsummarypage.h"
#include "wizardpulseconfigpage.h"
#include "wizardvalidationpage.h"
#include "batchsingle.h"

#ifdef BC_LIF
#include "wizardlifconfigpage.h"
#endif

#ifdef BC_MOTOR
#include "wizardmotorscanconfigpage.h"
#endif

ExperimentWizard::ExperimentWizard(QWidget *parent) :
    QWizard(parent)
{
    setWindowTitle(QString("Experiment Setup"));

    auto startPage = new WizardStartPage(this);
    d_pages << startPage;

    auto rfConfigPage = new WizardRfConfigPage(this);
    d_pages << rfConfigPage;

    auto chirpConfigPage = new WizardChirpConfigPage(this);
    d_pages << chirpConfigPage;

    auto ftmwConfigPage = new WizardFtmwConfigPage(this);
    d_pages << ftmwConfigPage;

    auto pulseConfigPage = new WizardPulseConfigPage(this);
    d_pages << ftmwConfigPage;

    auto validationPage = new WizardValidationPage(this);
    d_pages << validationPage;

    auto summaryPage = new WizardSummaryPage(this);
    d_pages << summaryPage;

    setPage(StartPage,startPage);
    setPage(RfConfigPage,rfConfigPage);
    setPage(ChirpConfigPage,chirpConfigPage);
    setPage(FtmwConfigPage,ftmwConfigPage);
    setPage(PulseConfigPage,pulseConfigPage);
    setPage(ValidationPage,validationPage);
    setPage(SummaryPage,summaryPage);


#ifdef BC_LIF
    auto lifConfigPage = new WizardLifConfigPage(this);
    d_pages << lifConfigPage;
    connect(this,&ExperimentWizard::newTrace,lifConfigPage,&WizardLifConfigPage::newTrace);
    connect(this,&ExperimentWizard::scopeConfigChanged,lifConfigPage,&WizardLifConfigPage::scopeConfigChanged);
    connect(lifConfigPage,&WizardLifConfigPage::updateScope,this,&ExperimentWizard::updateScope);
    connect(lifConfigPage,&WizardLifConfigPage::lifColorChanged,this,&ExperimentWizard::lifColorChanged);
    setPage(LifConfigPage,lifConfigPage);
#endif

#ifdef BC_MOTOR
    auto motorScanConfigPage = new WizardMotorScanConfigPage(this);
    d_pages << motorScanConfigPage;
    setPage(MotorScanConfigPage,motorScanConfigPage);
#endif

    for(int i=0; i<d_pages.size(); i++)
        connect(d_pages.at(i),&ExperimentWizardPage::experimentUpdate,this,&ExperimentWizard::updateExperiment);

    d_experiment = Experiment::loadFromSettings();
}

ExperimentWizard::~ExperimentWizard()
{ 
}

void ExperimentWizard::setPulseConfig(const PulseGenConfig c)
{
    d_experiment.setPulseGenConfig(c);
}

void ExperimentWizard::setFlowConfig(const FlowConfig c)
{
    d_experiment.setFlowConfig(c);
}

Experiment ExperimentWizard::getExperiment() const
{
    return d_experiment;
}

bool ExperimentWizard::sleepWhenDone() const
{
    return field(QString("sleep")).toBool();
}
