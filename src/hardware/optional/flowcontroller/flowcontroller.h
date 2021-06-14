#ifndef FLOWCONTROLLER_H
#define FLOWCONTROLLER_H

#include <src/hardware/core/hardwareobject.h>

#include <QTimer>

#include <src/data/experiment/flowconfig.h>

namespace BC::Key {
static const QString flowController("flowController");
static const QString flowChannels("numChannels");
}

class FlowController : public HardwareObject
{
    Q_OBJECT
public:
    FlowController(const QString subKey, const QString name, CommunicationProtocol::CommType commType,
                   int numChannels, QObject *parent = nullptr, bool threaded = false, bool critical = false);
    virtual ~FlowController();

    FlowConfig config() const { return d_config; }

signals:
    void channelNameUpdate(int,QString,QPrivateSignal);
    void flowUpdate(int,double,QPrivateSignal);
    void pressureUpdate(double,QPrivateSignal);
    void flowSetpointUpdate(int,double,QPrivateSignal);
    void pressureSetpointUpdate(double,QPrivateSignal);
    void pressureControlMode(bool,QPrivateSignal);

public slots:
    void setChannelName(const int ch, const QString name);
    void setPressureControlMode(bool enabled);
    void setFlowSetpoint(const int ch, const double val);
    void setPressureSetpoint(const double val);
    void readFlowSetpoint(const int ch);
    void readPressureSetpoint();
    void readFlow(const int ch);
    void readPressure();
    void readPressureControlMode();



    void updateInterval();
    virtual void poll();

private:
    virtual void hwSetPressureControlMode(bool enabled) =0;
    virtual void hwSetFlowSetpoint(const int ch, const double val) =0;
    virtual void hwSetPressureSetpoint(const double val) =0;
    virtual double hwReadFlowSetpoint(const int ch) =0;
    virtual double hwReadPressureSetpoint() =0;
    virtual double hwReadFlow(const int ch) =0;
    virtual double hwReadPressure() =0;
    virtual int hwReadPressureControlMode() =0;

protected:
    void initialize() override final;
    bool testConnection() override final;
    virtual void fcInitialize() =0;
    virtual bool fcTestConnection() =0;

    FlowConfig d_config;
    QTimer *p_readTimer;
    const int d_numChannels;

    void readAll();



    // HardwareObject interface
protected:
    virtual QList<QPair<QString, QVariant> > readAuxPlotData() override;
};

#if BC_FLOWCONTROLLER == 1
#include "mks647c.h"
class Mks647c;
typedef Mks647c FlowControllerHardware;
#elif BC_FLOWCONTROLLER == 2
#include "mks946.h"
class Mks946;
typedef Mks946 FlowControllerHardware;
#else
#include "virtualflowcontroller.h"
class VirtualFlowController;
typedef VirtualFlowController FlowControllerHardware;
#endif

#endif // FLOWCONTROLLER_H
