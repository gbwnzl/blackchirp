#ifndef EXPERIMENT_H
#define EXPERIMENT_H

#include <memory>

#include <QPair>
#include <QDateTime>
#include <QMetaType>

#include <data/storage/headerstorage.h>
#include <data/experiment/ftmwconfig.h>
#include <data/datastructs.h>
#include <data/experiment/pulsegenconfig.h>
#include <data/experiment/flowconfig.h>
#include <data/experiment/ioboardconfig.h>

//these are included because they define datatypes needed by qRegisterMetaType in main.cpp
#include <data/analysis/ft.h>
#include <data/analysis/ftworker.h>

#ifdef BC_LIF
#include <modules/lif/data/lifconfig.h>
#endif

#ifdef BC_MOTOR
#include <modules/motor/data/motorscan.h>
#endif

namespace BC::Store::Exp {
static const QString key("Experiment");
static const QString num("Number");
static const QString timeData("TimeDataInterval");
static const QString autoSave("AutoSaveShotsInterval");
static const QString ftmwEn("FtmwEnabled");
static const QString ftmwType("FtmwType");
}

class Experiment : private HeaderStorage
{
    Q_GADGET
public:
    Experiment();
    Experiment(const Experiment &other);
    Experiment(const int num, QString exptPath = QString(""), bool headerOnly = false);
    ~Experiment();

    bool d_hardwareSuccess{false};
    int d_number{0};
    QDateTime d_startTime;
    int d_timeDataInterval{300};
    int d_autoSaveIntervalHours{10000};
    QString d_errorString;
    QString d_startLogMessage;
    QString d_endLogMessage;
    BlackChirp::LogMessageCode d_endLogMessageCode{BlackChirp::LogNormal};

    inline bool isAborted()  const { return d_isAborted; }
    inline bool isDummy() const { return d_isDummy; }


    inline bool ftmwEnabled() const { return pu_ftmwConfig.get() != nullptr; }
    FtmwConfig* enableFtmw(FtmwConfig::FtmwType type);
    inline FtmwConfig* ftmwConfig() const {return pu_ftmwConfig.get(); }
    bool incrementFtmw();
    void setFtmwClocksReady();


    inline PulseGenConfig pGenConfig() const { return d_pGenCfg; }
    inline FlowConfig flowConfig() const { return d_flowCfg; }
    inline IOBoardConfig iobConfig() const { return d_iobCfg; }
    bool isComplete() const;
    QMap<QString,QPair<QList<QVariant>,bool>> timeDataMap() const;
    BlackChirp::LogMessageCode endLogMessageCode() const;
    QMap<QString, QPair<QVariant,QString>> headerMap() const;
    QMap<QString,BlackChirp::ValidationItem> validationItems() const;

    void setIOBoardConfig(const IOBoardConfig cfg);
    void setPulseGenConfig(const PulseGenConfig c) { d_pGenCfg = c; }
    void setFlowConfig(const FlowConfig c) { d_flowCfg = c; }

    bool addTimeData(const QList<QPair<QString, QVariant> > dataList, bool plot);
    void addTimeStamp();
    void setValidationItems(const QMap<QString,BlackChirp::ValidationItem> m);
    void addValidationItem(const QString key, const double min, const double max);
    void addValidationItem(const BlackChirp::ValidationItem &i);
//    void finalizeFtmwSnapshots(const FtmwConfig final);

#ifdef BC_LIF
    bool isLifWaiting() const;
    LifConfig lifConfig() const;
    void setLifEnabled(bool en = true);
    void setLifWaiting(bool wait);
    void setLifConfig(const LifConfig cfg);
    bool addLifWaveform(const LifTrace t);
#endif

#ifdef BC_MOTOR
    MotorScan motorScan() const;
    void setMotorEnabled(bool en = true);
    void setMotorScan(const MotorScan s);
    bool addMotorTrace(const QVector<double> d);
#endif

    bool initialize();
    void abort();
    bool snapshotReady();
    void finalSave();

    bool saveConfig();
    bool saveHeader();
    bool saveChirpFile() const;
    bool saveClockFile() const;
    bool saveTimeFile() const;
    QString timeDataText() const;
    void snapshot(int snapNum, const Experiment other);

    void saveToSettings() const;
//    static Experiment loadFromSettings();


private:
    quint64 d_lastSnapshot{0};
    bool d_isAborted{false};
    bool d_isDummy{false};

    std::unique_ptr<FtmwConfig> pu_ftmwConfig;
    PulseGenConfig d_pGenCfg;
    FlowConfig d_flowCfg;
    IOBoardConfig d_iobCfg;
    QMap<QString,QPair<QList<QVariant>,bool>> d_timeDataMap;
    QMap<QString,BlackChirp::ValidationItem> d_validationConditions;

    QString d_path;

#ifdef BC_LIF
    LifConfig d_lifCfg;
    bool d_waitForLifSet{false};
#endif

#ifdef BC_MOTOR
    MotorScan d_motorScan;
#endif

    // HeaderStorage interface
protected:
    void prepareToSave() override;
    void loadComplete() override;
};

Q_DECLARE_METATYPE(Experiment)
Q_DECLARE_METATYPE(std::shared_ptr<Experiment>)

#endif // EXPERIMENT_H
