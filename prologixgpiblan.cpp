#include "prologixgpiblan.h"

#include "tcpinstrument.h"

PrologixGpibLan::PrologixGpibLan(QObject *parent) :
    GpibController(parent)
{
    d_subKey = QString("prologixGpibLan");
    d_prettyName = QString("Prologix GPIB-LAN Controller");

    p_comm = new TcpInstrument(d_key,d_subKey,this);
    connect(p_comm,&CommunicationProtocol::logMessage,this,&PrologixGpibLan::logMessage);
    connect(p_comm,&CommunicationProtocol::hardwareFailure,this,&PrologixGpibLan::hardwareFailure);
}



bool PrologixGpibLan::testConnection()
{
    if(!p_comm->testConnection())
    {
        emit connected(false);
        return false;
    }

    QByteArray resp = p_comm->queryCmd(QString("++ver\n"));
    if(resp.isEmpty())
    {
        emit connected(false,QString("%1 gave a null response to ID query").arg(d_prettyName));
        return false;
    }
    if(!resp.startsWith("Prologix GPIB-ETHERNET Controller"))
    {
        emit connected(false,QString("%1 response invalid. Received: %2").arg(d_prettyName).arg(QString(resp)));
        return false;
    }

    emit logMessage(QString("%1 ID response: %2").arg(d_prettyName).arg(QString(resp)));

    p_comm->writeCmd(QString("++auto 0\n"));
    p_comm->writeCmd(QString("++savecfg 0\n"));
    p_comm->writeCmd(QString("++read_tmo_ms 50\n"));

    readAddress();

    emit connected();
    return true;
}

void PrologixGpibLan::initialize()
{
    p_comm->setReadOptions(1000,true,QByteArray("\n"));
    p_comm->initialize();
    testConnection();
}

bool PrologixGpibLan::readAddress()
{
    bool success = false;
    QByteArray resp = p_comm->queryCmd(QString("++addr\n"));
    d_currentAddress = resp.trimmed().toInt(&success);
    if(!success)
    {
        emit hardwareFailure();
        emit logMessage(QString("Could not read address. Response: %1 (Hex: %2)").arg(QString(resp)).arg(QString(resp.toHex())));
    }
    return success;
}

bool PrologixGpibLan::setAddress(int a)
{
    if(!p_comm->writeCmd(QString("++addr %1\n").arg(a)))
        return false;

    if(!readAddress())
        return false;

    if(d_currentAddress != a)
    {
        emit hardwareFailure();
        emit logMessage(QString("Address was not set to %1. Current address: %2").arg(a).arg(d_currentAddress),BlackChirp::LogError);
        return false;
    }

    return true;
}

QString PrologixGpibLan::queryTerminator() const
{
    return QString("++read eoi\n");
}