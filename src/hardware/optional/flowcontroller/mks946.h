// #ifndef MKS947_H
// #define MKS947_H

// #include <hardware/optional/flowcontroller/flowcontroller.h>

// namespace BC::Key::Flow {
// static const QString mks947{"mks947"};
// static const QString mks947Name("MKS 946 Flow Controller");
// static const QString address{"mksaddress"};
// static const QString pressureChannel{"pressureChannel"};
// static const QString offset{"channelOffset"};
// }

// class Mks946 : public FlowController
// {
//     Q_OBJECT
// public:
//     explicit Mks946(QObject *parent = nullptr);

//     // HardwareObject interface
// protected:
//     bool fcTestConnection() override;

//     // FlowController interface
// public slots:
//     void hwSetFlowSetpoint(const int ch, const double val) override;
//     void hwSetPressureSetpoint(const double val) override;
//     double hwReadFlowSetpoint(const int ch) override;
//     double hwReadPressureSetpoint() override;
//     double hwReadFlow(const int ch) override;
//     double hwReadPressure() override;
//     void hwSetPressureControlMode(bool enabled) override;
//     int hwReadPressureControlMode() override;
//     void poll() override;

// protected:
//     void fcInitialize() override;

// private: // GW trying stuff
//     bool mksWrite(QString cmd);
//     QByteArray mksQuery(QString cmd);

//     // HardwareObject interface
// public slots:
//     void sleep(bool b) override;

// private:
//     int d_nextRead;
//     QString d_errorString;
// };

// #endif // MKS947_H

#ifndef MKS946_H         // ← was MKS947_H
#define MKS946_H

#include <hardware/optional/flowcontroller/flowcontroller.h>
#include <QString>       // make sure QString is visible here

namespace BC::Key::Flow {
static const QString mks947{"mks947"}; // keep if intentional; else rename to mks946
static const QString mks947Name("MKS 946 Flow Controller");
static const QString address{"mksaddress"};
static const QString pressureChannel{"pressureChannel"};
static const QString offset{"channelOffset"};
}

class Mks946 : public FlowController
{
    Q_OBJECT
public:
    explicit Mks946(QObject *parent = nullptr);

protected:
    bool fcTestConnection() override;

public slots:
    void   hwSetFlowSetpoint(const int ch, const double val) override;
    void   hwSetPressureSetpoint(const double val) override;
    double hwReadFlowSetpoint(const int ch) override;
    double hwReadPressureSetpoint() override;
    double hwReadFlow(const int ch) override;
    double hwReadPressure() override;
    void   hwSetPressureControlMode(bool enabled) override;
    int    hwReadPressureControlMode() override;
    void   poll() override;

protected:
    void fcInitialize() override;

private:
    // ↓↓↓ NEW: helpers you’ll implement in the .cpp
    bool ensurePidOff(QString *why = nullptr);
    bool ensurePidOn (QString *why = nullptr);

    // existing low-level I/O
    bool       mksWrite(QString cmd);
    QByteArray mksQuery(QString cmd);

public slots:
    void sleep(bool b) override;

private:
    int     d_nextRead;
    QString d_errorString;
};

#endif // MKS946_H
