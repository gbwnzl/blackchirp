#ifndef DSA71604C_H
#define DSA71604C_H

#include "ftmwscope.h"

#include <QTimer>
#include <QAbstractSocket>

class QTcpSocket;

class Dsa71604c : public FtmwScope
{
    Q_OBJECT
public:
    Dsa71604c(QObject *parent = nullptr);
    ~Dsa71604c();

    // HardwareObject interface
public slots:
    void readSettings() override;
    Experiment prepareForExperiment(Experiment exp) override;
    void beginAcquisition() override;
    void endAcquisition() override;

    void readWaveform() override;
    void wakeUp();
    void socketError(QAbstractSocket::SocketError e);

protected:
    bool testConnection() override;
    void initialize() override;


private:
    bool d_waitingForReply;
    bool d_foundHeader;
    int d_headerNumBytes;
    int d_waveformBytes;
    QTimer *p_scopeTimeout;

    QByteArray scopeQueryCmd(QString query);
    QTcpSocket *p_socket;
};

#endif // DSA71604C_H
