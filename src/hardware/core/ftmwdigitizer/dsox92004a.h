#ifndef DSOX92004A_H
#define DSOX92004A_H

#include <hardware/core/ftmwdigitizer/ftmwscope.h>

class QTcpSocket;

namespace BC::Key::FtmwScope {
static const QString dsox92004a{"DSOx92004A"};
static const QString dsox92004aName("Ftmw Oscilloscope DSOx92004A");
}

class DSOx92004A : public FtmwScope
{
    Q_OBJECT
public:
    explicit DSOx92004A(QObject *parent = nullptr);

    // HardwareObject interface
public slots:
    bool prepareForExperiment(Experiment &exp) override;
    void beginAcquisition() override;
    void endAcquisition() override;

protected:
    void initialize() override;
    bool testConnection() override;

    // FtmwScope interface
public slots:
    void readWaveform() override;

private:
    QTcpSocket *p_socket;
    QTimer *p_queryTimer;
    void retrieveData();
    bool scopeCommand(QString cmd);

    bool d_acquiring{false};
    bool d_processing{false};

};

#endif // DSOX92004A_H
