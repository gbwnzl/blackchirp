#include "wizardstartpage.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>

#include "experimentwizard.h"

WizardStartPage::WizardStartPage(QWidget *parent) :
    ExperimentWizardPage(parent)
{
    setTitle(QString("Configure Experiment"));
    setSubTitle(QString("Choose which type(s) of experiment you wish to perform."));

    QFormLayout *fl = new QFormLayout(this);

    p_ftmw = new QGroupBox(QString("FTMW"),this);
    p_ftmw->setCheckable(true);
    connect(p_ftmw,&QGroupBox::toggled,this,&WizardStartPage::completeChanged);

    p_ftmwTypeBox = new QComboBox(this);
    p_ftmwTypeBox->addItem(QString("Target Shots"),QVariant::fromValue(BlackChirp::FtmwTargetShots));
    p_ftmwTypeBox->addItem(QString("Target Time"),QVariant::fromValue(BlackChirp::FtmwTargetTime));
    p_ftmwTypeBox->addItem(QString("Forever"),QVariant::fromValue(BlackChirp::FtmwForever));
    p_ftmwTypeBox->addItem(QString("Peak Up"),QVariant::fromValue(BlackChirp::FtmwPeakUp));
    p_ftmwTypeBox->addItem(QString("LO Scan"),QVariant::fromValue(BlackChirp::FtmwLoScan));
    p_ftmwTypeBox->setCurrentIndex(0);

    auto lbl = new QLabel(QString("Type"));
    lbl->setAlignment(Qt::AlignRight|Qt::AlignCenter);
    lbl->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Expanding);
    fl->addRow(lbl,p_ftmwTypeBox);

    p_ftmwShotsBox = new QSpinBox(this);
    p_ftmwShotsBox->setRange(1,__INT_MAX__);
    p_ftmwShotsBox->setToolTip(QString("Number of FIDs to average.\n"
                                       "When this number is reached, the experiment ends in Target Shots mode, while an exponentially weighted moving average engages in Peak Up mode.\n\n"
                                       "If this box is disabled, it is either irrelevant or will be configured on a later page (e.g. in Multiple LO mode)."));
    p_ftmwShotsBox->setSingleStep(5000);

    lbl = new QLabel(QString("Shots"));
    lbl->setAlignment(Qt::AlignRight|Qt::AlignCenter);
    lbl->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Expanding);
    fl->addRow(lbl,p_ftmwShotsBox);

    p_ftmwTargetTimeBox = new QDateTimeEdit(this);
    p_ftmwTargetTimeBox->setDisplayFormat(QString("yyyy-MM-dd h:mm:ss AP"));
    p_ftmwTargetTimeBox->setMaximumDateTime(QDateTime::currentDateTime().addSecs(__INT_MAX__));
    p_ftmwTargetTimeBox->setCurrentSection(QDateTimeEdit::HourSection);
    p_ftmwTargetTimeBox->setToolTip(QString("The time at which an experiment in Target Time mode will complete. If disabled, this setting is irrelevant."));

    lbl = new QLabel(QString("Stop Time"));
    lbl->setAlignment(Qt::AlignRight|Qt::AlignCenter);
    lbl->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Expanding);
    fl->addRow(lbl,p_ftmwTargetTimeBox);

    p_phaseCorrectionBox = new QCheckBox(this);
    p_phaseCorrectionBox->setToolTip(QString("If checked, Blackchirp will optimize the autocorrelation of the chirp during the acquisition.\n\nFor this to work, the chirp must be part of the signal recorded by the digitizer."));

    lbl = new QLabel(QString("Phase Correction"));
    lbl->setAlignment(Qt::AlignRight|Qt::AlignCenter);
    lbl->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Expanding);
    fl->addRow(lbl,p_phaseCorrectionBox);

    p_chirpScoringBox = new QCheckBox(this);
    p_chirpScoringBox->setToolTip(QString("If checked, Blackchirp will compare the RMS of the chirp in each new waveform with that of the current average chirp RMS.\nIf less than threshold*averageRMS, the FID will be rejected."));
    lbl = new QLabel(QString("Chirp Scoring"));
    lbl->setAlignment(Qt::AlignRight|Qt::AlignCenter);
    lbl->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Expanding);
    fl->addRow(lbl,p_chirpScoringBox);

    p_thresholdBox = new QDoubleSpinBox(this);
    p_thresholdBox->setRange(0.0,1.0);
    p_thresholdBox->setSingleStep(0.05);
    p_thresholdBox->setValue(0.9);
    p_thresholdBox->setDecimals(3);
    lbl = new QLabel(QString("Chirp Threshold"));
    lbl->setAlignment(Qt::AlignRight|Qt::AlignCenter);
    lbl->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Expanding);
    fl->addRow(lbl,p_thresholdBox);

    p_chirpOffsetBox = new QDoubleSpinBox(this);
    p_chirpOffsetBox->setRange(-0.00001,100.0);
    p_chirpOffsetBox->setDecimals(5);
    p_chirpOffsetBox->setSingleStep(0.1);
    p_chirpOffsetBox->setValue(-1.0);
    p_chirpOffsetBox->setSuffix(QString::fromUtf16(u" μs"));
    p_chirpOffsetBox->setSpecialValueText(QString("Automatic"));
    p_chirpOffsetBox->setToolTip(QString("The time at which the chirp starts (used for phase correction and chirp scoring).\n\nIf automatic, Blackchirp assumes the digitizer is triggered at the start of the protection pulse,\nand accounts for the digitizer trigger position."));
    lbl = new QLabel(QString("Chirp Start"));
    lbl->setAlignment(Qt::AlignRight|Qt::AlignCenter);
    lbl->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Expanding);
    fl->addRow(lbl,p_chirpOffsetBox);

    p_ftmw->setLayout(fl);

    auto *fl2 = new QFormLayout(this);

    auto *sgb = new QGroupBox(QString("Common Settings"));
    p_auxDataIntervalBox = new QSpinBox(this);
    p_auxDataIntervalBox->setRange(5,__INT_MAX__);
    p_auxDataIntervalBox->setValue(300);
    p_auxDataIntervalBox->setSingleStep(300);
    p_auxDataIntervalBox->setSuffix(QString(" s"));
    p_auxDataIntervalBox->setToolTip(QString("Interval for auxilliary data readings (e.g., flows, pressure, etc.)"));
    lbl = new QLabel(QString("Time Data Interval"));
    lbl->setAlignment(Qt::AlignRight|Qt::AlignCenter);
    lbl->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Expanding);
    fl2->addRow(lbl,p_auxDataIntervalBox);

    p_snapshotBox = new QSpinBox(this);
    p_snapshotBox->setRange(1<<8,(1<<30)-1);
    p_snapshotBox->setValue(20000);
    p_snapshotBox->setSingleStep(5000);
    p_snapshotBox->setPrefix(QString("every "));
    p_snapshotBox->setSuffix(QString(" shots"));
    p_snapshotBox->setToolTip(QString("Interval for taking experiment snapshots (i.e., autosaving)."));

    lbl = new QLabel(QString("Snapshot Interval"));
    lbl->setAlignment(Qt::AlignRight|Qt::AlignCenter);
    lbl->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Expanding);
    fl2->addRow(lbl,p_snapshotBox);
    sgb->setLayout(fl2);



#ifndef BC_LIF

    p_ftmw->setChecked(true);
#ifndef BC_MOTOR
    p_ftmw->setCheckable(false);
#endif
#else
    p_lif = new QGroupBox(QString("LIF"),this);
    p_lif->setCheckable(true);
    connect(p_lif,&QGroupBox::toggled,this,&WizardStartPage::completeChanged);
#endif

#ifdef BC_MOTOR
    p_motor = new QGroupBox(QString("Motor Scan"),this);
    p_motor->setCheckable(true);
    connect(p_motor,&QGroupBox::toggled,this,&WizardStartPage::completeChanged);
    connect(p_motor,&QGroupBox::toggled,[=](bool ch){
        p_ftmw->setDisabled(ch);
        if(ch)
            p_ftmw->setChecked(false);
#ifdef BC_LIF
        p_lif->setDisabled(ch);
        if(ch)
            p_lif->setChecked(false);
#endif
    });
#endif

    auto *hbl = new QHBoxLayout();
    hbl->addWidget(p_ftmw);
#ifdef BC_LIF
    hbl->addWidget(p_lif);
#endif
#ifdef BC_MOTOR
    hbl->addWidget(p_motor);
#endif

    auto *vbl = new QVBoxLayout();
    vbl->addLayout(hbl,1);
    vbl->addWidget(sgb);

    setLayout(vbl);

    connect(p_ftmwTypeBox,&QComboBox::currentTextChanged,this,&WizardStartPage::configureUI);
    connect(p_chirpScoringBox,&QCheckBox::toggled,this,&WizardStartPage::configureUI);
    connect(p_phaseCorrectionBox,&QCheckBox::toggled,this,&WizardStartPage::configureUI);
}

WizardStartPage::~WizardStartPage()
{
}


int WizardStartPage::nextId() const
{
#ifdef BC_MOTOR
    if(p_motor->isChecked())
        return ExperimentWizard::MotorScanConfigPage;
#endif

#ifdef BC_LIF
    if(p_lif->isChecked())
        return ExperimentWizard::LifConfigPage;
    else
        return ExperimentWizard::RfConfigPage;
#endif

    return ExperimentWizard::RfConfigPage;
}

bool WizardStartPage::isComplete() const
{
    if(!p_ftmw->isCheckable())
        return true;

    bool out = p_ftmw->isChecked();
#ifdef BC_LIF
    out = out || p_lif->isChecked();
#endif

#ifdef BC_MOTOR
    out = out || p_motor->isChecked();
#endif

    return out;
}

void WizardStartPage::initializePage()
{
    auto e = getExperiment();

#ifdef BC_LIF
    p_ftmw->setChecked(e.ftmwConfig().isEnabled());
    p_lif->setChecked(e.lifConfig().isEnabled());
#endif

#ifdef BC_MOTOR
    if(e.motorScan().isEnabled())
    {
        p_motor->setChecked(true);
        p_ftmw->setEnabled(false);
        p_ftmw->setChecked(false);
#ifdef BC_LIF
        p_lif->setChecked(false);
        p_lif->setEnabled(false);
#endif
    }
    else
    {
        p_motor->setChecked(false);
    }
#endif

    p_ftmwTypeBox->setCurrentIndex(p_ftmwTypeBox->findData(QVariant::fromValue(e.ftmwConfig().type())));
    auto shots = e.ftmwConfig().targetShots();
    if(e.ftmwConfig().hasMultiFidLists())
        shots = e.ftmwConfig().rfConfig().shotsPerClockStep();
    p_ftmwShotsBox->setValue(shots);
    p_ftmwTargetTimeBox->setMinimumDateTime(QDateTime::currentDateTime().addSecs(60));
    p_ftmwTargetTimeBox->setDateTime(QDateTime::currentDateTime().addSecs(3600));
    p_phaseCorrectionBox->setChecked(e.ftmwConfig().isPhaseCorrectionEnabled());
    p_chirpScoringBox->setChecked(e.ftmwConfig().isChirpScoringEnabled());
    p_thresholdBox->setValue(e.ftmwConfig().chirpRMSThreshold());

    ///TODO: use chirp offset!

    p_snapshotBox->setValue(e.autoSaveShots());
    p_auxDataIntervalBox->setValue(e.timeDataInterval());

    configureUI();
}

bool WizardStartPage::validatePage()
{
    ///TODO: In the future, allow user to choose old experiment to repeat!
    ///Be sure to give user the options to use current pulse settings.
    /// Allow changing flow settings?
     auto e = getExperiment();

     auto ftc = e.ftmwConfig();
     ftc.setType(p_ftmwTypeBox->currentData().value<BlackChirp::FtmwType>());
     ftc.setTargetShots(p_ftmwShotsBox->value());
     ftc.setTargetTime(p_ftmwTargetTimeBox->dateTime());
     ftc.setChirpScoringEnabled(p_chirpScoringBox->isChecked());
     ftc.setChirpRMSThreshold(p_thresholdBox->value());
     ftc.setPhaseCorrectionEnabled(p_phaseCorrectionBox->isChecked());
     ///TODO: use offset info!

     e.setFtmwConfig(ftc);
     if(p_ftmw->isCheckable())
         e.setFtmwEnabled(p_ftmw->isChecked());
     else
         e.setFtmwEnabled(true);


#ifdef BC_LIF
     e.setLifEnabled(p_lif->isChecked());
#endif

#ifdef BC_MOTOR
     e.setMotorEnabled(p_motor->isChecked());
#endif

     e.setAutoSaveShotsInterval(p_snapshotBox->value());
     e.setTimeDataInterval(p_auxDataIntervalBox->value());

     emit experimentUpdate(e);
     return true;

}

void WizardStartPage::configureUI()
{
    auto type = p_ftmwTypeBox->currentData().value<BlackChirp::FtmwType>();
    switch(type)
    {
    case BlackChirp::FtmwForever:
    case BlackChirp::FtmwPeakUp:
    case BlackChirp::FtmwTargetShots:
        p_ftmwShotsBox->setEnabled(true);
        p_ftmwTargetTimeBox->setEnabled(false);
        break;
    case BlackChirp::FtmwTargetTime:
        p_ftmwShotsBox->setEnabled(false);
        p_ftmwTargetTimeBox->setEnabled(true);
        break;
    default:
        p_ftmwShotsBox->setEnabled(false);
        p_ftmwTargetTimeBox->setEnabled(false);
        break;
    }

    p_chirpOffsetBox->setEnabled(p_phaseCorrectionBox->isChecked() || p_chirpScoringBox->isChecked());
    p_thresholdBox->setEnabled(p_chirpScoringBox->isChecked());
}
