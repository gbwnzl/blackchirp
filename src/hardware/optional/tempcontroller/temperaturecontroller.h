#ifndef TEMPERATURECONTROLLER_H
#define TEMPERATURECONTROLLER_H
#include <hardware/core/hardwareobject.h>

namespace BC::Key {
static const QString tController("tempController");
}

class TemperatureController : public HardwareObject
{
    Q_OBJECT
public:
    explicit TemperatureController(const QString subKey, const QString name, CommunicationProtocol::CommType commType, QObject *parent =nullptr, bool threaded = false, bool critical = false);
    virtual ~TemperatureController();

signals:
    void temperatureUpdate(QList<double>, QPrivateSignal);

public slots:
    virtual QList<double> readTemperatures();

protected:
    int d_numChannels;
    QList<double> d_temperatureList;

    virtual void tcInitialize() =0;


    // HardwareObject interface
    virtual QList<QPair<QString, QVariant> > readAuxPlotData() override;
    void initialize() override final;
protected:
    virtual QList<double> readHWTemperature() = 0;
};

#endif // TEMPERATURECONTROLLER_H


#if BC_TEMPCONTROLLER == 1
#include "lakeshore218.h"
class Lakeshore218;
typedef Lakeshore218 TemperatureControllerHardware;
#else
#include "virtualtempcontroller.h"
class VirtualTemperatureController;
typedef VirtualTemperatureController TemperatureControllerHardware;
#endif



