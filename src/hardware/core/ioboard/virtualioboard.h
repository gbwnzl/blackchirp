#ifndef VIRTUALIOBOARD_H
#define VIRTUALIOBOARD_H

#include "ioboard.h"

class VirtualIOBoard : public IOBoard
{
    Q_OBJECT
public:
    explicit VirtualIOBoard(QObject *parent = nullptr);

    // HardwareObject interface
public slots:
    Experiment prepareForExperiment(Experiment exp) override;

    // HardwareObject interface
protected:
    bool testConnection() override;
    void initialize() override;
    virtual QList<QPair<QString, QVariant> > readAuxPlotData() override;
    virtual QList<QPair<QString, QVariant> > readAuxNoPlotData() override;

private:
    QList<QPair<QString, QVariant> > auxData(bool plot);

    // IOBoard interface
protected:
    void readIOBSettings() override;
};

#endif // VIRTUALIOBOARD_H
