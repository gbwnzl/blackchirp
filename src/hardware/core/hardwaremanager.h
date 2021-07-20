#ifndef HARDWAREMANAGER_H
#define HARDWAREMANAGER_H

#include <QObject>
#include <memory>
#include <data/datastructs.h>
#include <data/experiment/experiment.h>
#include <data/storage/auxdatastorage.h>
#include <data/storage/settingsstorage.h>

class HardwareObject;
class ClockManager;

namespace BC::Key {
static const QString hw("hardware");
static const QString allHw("instruments");
}

class HardwareManager : public QObject, public SettingsStorage
{
    Q_OBJECT
public:
    explicit HardwareManager(QObject *parent = 0);
    ~HardwareManager();

signals:
    void logMessage(const QString, const BlackChirp::LogMessageCode = BlackChirp::LogNormal);
    void statusMessage(const QString);
    void hwInitializationComplete();

    void allHardwareConnected(bool);
    /*!
     * \brief Emitted when a connection is being tested from the communication dialog
     * \param QString The HardwareObject key
     * \param bool Whether connection was successful
     * \param QString Error message
     */
    void testComplete(QString,bool,QString);
    void beginAcquisition();
    void abortAcquisition();
    void experimentInitialized(std::shared_ptr<Experiment>);
    void endAcquisition();
    void auxData(AuxDataStorage::AuxDataMap);
    void validationData(AuxDataStorage::AuxDataMap);
    void rollingData(AuxDataStorage::AuxDataMap,QDateTime);

    void ftmwScopeShotAcquired(QByteArray);

    void clockFrequencyUpdate(RfConfig::ClockType, double);
    void allClocksReady(QHash<RfConfig::ClockType,RfConfig::ClockFreq>);

    void pGenSettingUpdate(int,PulseGenConfig::Setting,QVariant);
    void pGenConfigUpdate(PulseGenConfig);
    void pGenRepRateUpdate(double);

    void flowUpdate(int,double);
    void flowSetpointUpdate(int,double);
    void gasPressureUpdate(double);
    void gasPressureSetpointUpdate(double);
    void gasPressureControlMode(bool);

#ifdef BC_PCONTROLLER
    void pressureControlReadOnly(bool);
    void pressureUpdate(double);
    void pressureSetpointUpdate(double);
    void pressureControlMode(bool);
#endif

#ifdef BC_LIF
    void lifScopeShotAcquired(LifTrace);
    void lifScopeConfigUpdated(BlackChirp::LifScopeConfig);
    void lifSettingsComplete(bool success = true);
    void lifLaserPosUpdate(double);
#endif

#ifdef BC_MOTOR
    void motorTraceAcquired(QVector<double> d);
    void motorMoveComplete(bool);
    void moveMotorToPosition(double x, double y, double z);
    void motorLimitStatus(MotorScan::MotorAxis axis, bool negLimit, bool posLimit);
    void motorPosUpdate(MotorScan::MotorAxis axis, double pos);
    void motorRest();
#endif

public slots:
    void initialize();

    /*!
     * \brief Records whether hardware connection was successful
     * \param obj A HardwareObject that was tested
     * \param success Whether communication was sucessful
     * \param msg Error message
     */
    void connectionResult(HardwareObject *obj, bool success, QString msg);

    /*!
     * \brief Sets hardware status in d_status to false, disables program
     * \param obj The object that failed.
     *
     * TODO: Consider generating an abort signal here
     */
    void hardwareFailure();

    void sleep(bool b);

    void initializeExperiment(std::shared_ptr<Experiment> exp);

    void testAll();
    void testObjectConnection(const QString type, const QString key);

    void getAuxData();

    void setClocks(QHash<RfConfig::ClockType,RfConfig::ClockFreq> clocks);

    void setPGenSetting(int index, PulseGenConfig::Setting s, QVariant val);
    void setPGenConfig(const PulseGenConfig c);
    void setPGenRepRate(double r);

    void setFlowSetpoint(int index, double val);
    void setGasPressureSetpoint(double val);
    void setGasPressureControlMode(bool en);

#ifdef BC_PCONTROLLER
    void setPressureSetpoint(double val);
    void setPressureControlMode(bool en);
    void openGateValve();
    void closeGateValve();
#endif

#ifdef BC_LIF
    void setLifParameters(double delay, double pos);
    bool setPGenLifDelay(double d);
    void setLifScopeConfig(const BlackChirp::LifScopeConfig c);
    bool setLifLaserPos(double pos);
#endif

public:
    std::map<QString,QStringList> validationKeys() const;

private:
    std::size_t d_responseCount{0};
    void checkStatus();

    std::map<QString,HardwareObject*> d_hardwareMap;
    std::unique_ptr<ClockManager> pu_clockManager;

    template<class T>
    T* findHardware(const QString key){
        auto it = d_hardwareMap.find(key);
        return it == d_hardwareMap.end() ? nullptr : static_cast<T*>(it->second);
    }

};

#endif // HARDWAREMANAGER_H
