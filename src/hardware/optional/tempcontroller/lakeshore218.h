#ifndef LAKESHORE218_H
#define LAKESHORE218_H
#include "temperaturecontroller.h"


class Lakeshore218 : public TemperatureController
{
    Q_OBJECT
public:
    explicit Lakeshore218(QObject* parent = nullptr);

    // HardwareObject interface
public slots:
    void readSettings() override;
    QStringList channelNames();
    // TemperatureController interface
protected:
    bool testConnection() override;
    void tcInitialize() override;

    QList<double> readHWTemperature() override;
private:
    QTimer *p_readTimer;



};

#endif // LAKESHORE218_H
