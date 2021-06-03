#ifndef EXPERIMENTWIZARD_H
#define EXPERIMENTWIZARD_H

#include <QWizard>

#include "experiment.h"

class BatchManager;
class ExperimentWizardPage;

class ExperimentWizard : public QWizard
{
    Q_OBJECT
public:
    ExperimentWizard(QWidget *parent = 0);
    ~ExperimentWizard();

    enum Page {
        StartPage,
        LoScanPage,
        RfConfigPage,
        ChirpConfigPage,
        DigitizerConfigPage,
#ifdef BC_LIF
        LifConfigPage,
#endif
#ifdef BC_MOTOR
        MotorScanConfigPage,
#endif
        PulseConfigPage,
        ValidationPage,
        SummaryPage
    };

    void setPulseConfig(const PulseGenConfig c);
    void setFlowConfig(const FlowConfig c);
    Experiment getExperiment() const;

public slots:
    void updateExperiment(const Experiment e) { d_experiment = e; }


private:    
    QList<ExperimentWizardPage*> d_pages;


    Experiment d_experiment;

#ifdef BC_LIF
public slots:
    void setCurrentLaserPos(double pos);

signals:
    void newTrace(LifTrace);
    void updateScope(BlackChirp::LifScopeConfig);
    void scopeConfigChanged(BlackChirp::LifScopeConfig);
    void lifColorChanged();
    void laserPosUpdate(double);

private:
    ExperimentWizardPage *p_lifConfigPage;
#endif

#ifdef BC_MOTOR
private:
    ExperimentWizardPage *p_motorScanConfigPage;
#endif


    // QWidget interface
public:
    virtual QSize sizeHint() const;
};

#endif // EXPERIMENTWIZARD_H
