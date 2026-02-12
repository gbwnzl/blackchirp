#include "mks946.h"

using namespace BC::Key::Flow;
Mks946::Mks946(QObject *parent) :
    FlowController(mks947,mks947Name,CommunicationProtocol::Rs232,parent),
    d_nextRead(0)
{
    if(!containsArray(channels))
    {
        std::vector<SettingsMap> l;
        int ch = get(flowChannels,4);
        l.reserve(ch);
        for(int i=0; i<ch; ++i)
            l.push_back({{chUnits,QString("sccm")},{chMax,10000.0},{chDecimals,1}});

        setArray(channels,l,true);
    }

    setDefault(address,253);
    setDefault(offset,1);
    setDefault(pressureChannel,5);

    setDefault(pUnits,QString("kTorr"));
    setDefault(pMax,10.0);
    setDefault(pDec,3);

}


bool Mks946::fcTestConnection()
{
    //GW
    if(!p_comm->bcTestConnection())
    {
        emit logMessage(QString("Cannot test connection."),LogHandler::Error);
        return false;
    }

    QByteArray resp = mksQuery(QString("MD?"));
    if(!resp.contains(QByteArray("946")))
    {
        d_errorString = QString("Received invalid response to model query. Response: %1").arg(QString(resp));
        return false;
    }

    emit logMessage(QString("Response: %1").arg(QString(resp)));
    return true;
}

void Mks946::hwSetFlowSetpoint(const int ch, const double val)
{
    if(!isConnected())
    {
        emit logMessage(QString("Cannot set flow setpoint due to a previous communication failure. Reconnect and try again."),LogHandler::Error);
        return;
    }

    bool pidActive = false;

    //first ensure recipe 1 is active
    if(!mksWrite(QString("RCP!1")))
    {
        if(d_errorString.contains(QString("166")))
        {
            pidActive = true;
            if(!mksWrite(QString("PID!OFF")))
            {
                emit logMessage(QString("Could not disable PID mode to update channel %1 setpoint. Error: %2").arg(ch+1).arg(d_errorString),LogHandler::Error);
                emit hardwareFailure();
                return;
            }
        }
        else
        {
            emit logMessage(d_errorString,LogHandler::Error);
            emit hardwareFailure();
            return;
        }
    }

    if(!pidActive)
    {
        //make sure ratio recipe 1 is active
        if(!mksWrite(QString("RRCP!1")))
        {
            emit logMessage(d_errorString,LogHandler::Error);
            emit hardwareFailure();
            return;
        }
    }

    double val2 = val/2.0;
    if(!mksWrite(QString("RRQ%1!%2").arg(ch+get(offset,1)).arg(val2,0,'E',2,QLatin1Char(' '))))
    {
        emit logMessage(d_errorString,LogHandler::Error);
        emit hardwareFailure();
        return;
    }

    if(pidActive)
    {
        hwSetPressureControlMode(true);
        return readFlowSetpoint(ch);
    }
}

void Mks946::hwSetPressureSetpoint(const double val)
{
    if(!isConnected())
    {
        emit logMessage(QString("Cannot set pressure setpoint due to a previous communication failure. Reconnect and try again."),LogHandler::Error);
        return;
    }

    bool pidActive = false;

    //first ensure recipe 1 is active
    if(!mksWrite(QString("RCP!1")))
    {
        if(d_errorString.contains(QString("166")))
        {
            pidActive = true;
            if(!mksWrite(QString("PID!OFF")))
            {
                emit logMessage(QString("Could not disable PID mode to change pressure setpoint. Error: %1").arg(d_errorString));
                emit hardwareFailure();
                return;
            }
        }
        else
        {
            emit logMessage(d_errorString,LogHandler::Error);
            emit hardwareFailure();
            return;
        }
    }

    if(!mksWrite(QString("RPSP!%1").arg(val*1000.0,1,'E',2,QLatin1Char(' '))))
    {
        emit logMessage(d_errorString,LogHandler::Error);
        emit hardwareFailure();
        return;
    }

    if(pidActive)
        hwSetPressureControlMode(true);
}

double Mks946::hwReadFlowSetpoint(const int ch)
{
    if(!isConnected())
        return 0.0;

    QByteArray resp = mksQuery(QString("RRQ%1?").arg(ch+get(offset,1)));
    bool ok = false;
    double out = resp.mid(2).toDouble(&ok);
    if(!ok)
    {
        emit logMessage(QString("Received invalid response to channel %1 setpoint query. Response: %2").arg(ch+1).arg(QString(resp)),LogHandler::Error);
        emit hardwareFailure();
        return -1.0;
    }

    return out*2.0;
}

double Mks946::hwReadPressureSetpoint()
{
    if(!isConnected())
        return 0.0;

    QByteArray resp = mksQuery(QString("RPSP?"));
    bool ok = false;
    double out = resp.mid(2).toDouble(&ok);
    if(!ok)
    {
        emit logMessage(QString("Received invalid response to pressure setpoint query. Response: %1").arg(QString(resp)),LogHandler::Error);
        emit hardwareFailure();
        return -1.0;
    }

    return out/1000.0; // convert to kTorr
}

double Mks946::hwReadFlow(const int ch)
{
    if(!isConnected())
        return 0.0;

    QByteArray resp = mksQuery(QString("FR%1?").arg(ch+get(offset,1)));
    if(resp.contains(QByteArray("MISCONN")))
        return 0.0;

    bool ok = false;
    double out = resp.toDouble(&ok);
    if(!ok)
    {
        emit logMessage(QString("Received invalid response to flow query for channel %1. Response: %2").arg(ch+1).arg(QString(resp)),LogHandler::Error);
        emit hardwareFailure();
        return -1.0;
    }

    return out;
}

double Mks946::hwReadPressure()
{
    if(!isConnected())
        return 0.0;

    QByteArray resp = mksQuery(QString("PR%1?").arg(get(pressureChannel,5)));
    if(resp.contains(QByteArray("LO")))
        return 0.0;

    if(resp.contains(QByteArray("MISCONN")))
    {
        emit logMessage(QString("No pressure gauge connected."),LogHandler::Warning);
        setPressureControlMode(false);
        emit hardwareFailure();
        return -0.0;
    }

    if(resp.contains(QByteArray("ATM")))
        return get(pMax,10.0);

    bool ok = false;
    double out = resp.toDouble(&ok);
    if(ok)
        return out/1000.0; //convert to kTorr

    emit logMessage(QString("Could not parse reply to pressure query. Response: %1").arg(QString(resp)),LogHandler::Error);
    emit hardwareFailure();
    return -1.0;
}


bool Mks946::ensurePidOff(QString *why)
{
    QByteArray q = mksQuery("PID?");
    if (q.contains("OFF")) return true;
    if (!mksWrite("PID!OFF")) {
        // If the failure was 169, re-check; it can be a harmless “already off / not applicable”
        if (d_errorString.contains("169")) {
            QByteArray q2 = mksQuery("PID?");
            if (q2.contains("OFF")) return true;
            if (why) *why = "Controller rejected PID!OFF (169) but still reports PID ON.";
            return false;
        }
        if (why) *why = "Controller rejected PID!OFF with: " + d_errorString;
        return false;
    }
    // verify
    QByteArray q3 = mksQuery("PID?");
    return q3.contains("OFF");
}

bool Mks946::ensurePidOn(QString *why)
{
    QByteArray q = mksQuery("PID?");
    if (q.contains("ON")) return true;
    if (!mksWrite("PID!ON")) {
        if (why) *why = "Controller rejected PID!ON with: " + d_errorString;
        return false;
    }
    QByteArray q2 = mksQuery("PID?");
    return q2.contains("ON");
}


// void Mks946::hwSetPressureControlMode(bool enabled)
// {
//     if(!isConnected())
//     {
//         emit logMessage(QString("Cannot set pressure control mode due to a previous communication failure. Reconnect and try again."),LogHandler::Error);
//         return;
//     }

//     if(enabled)
//     {
//         //first ensure recipe 1 is active
//         // if(!mksWrite(QString("RCP!1")))
//         // {
//         //     emit logMessage(d_errorString,LogHandler::Error);
//         //     emit hardwareFailure();
//         //     return;
//         // }

//         // if(!mksWrite(QString("RRCP!1")))
//         // {
//         //     emit logMessage(d_errorString,LogHandler::Error);
//         //     emit hardwareFailure();
//         //     return;
//         // }

//         QList<QString> chNames;
//         chNames << QString("A1") << QString("A2") << QString("B1") << QString("B2") << QString("C1") << QString("C2");

//         //ensure pressure sensor is set to control channel
//         if(!mksWrite(QString("RPCH!%1").arg(chNames.at(get(pressureChannel,5)-1))))
//         {
//             emit logMessage(d_errorString,LogHandler::Error);
//             emit hardwareFailure();
//             return;
//         }

//         if(!mksWrite(QString("RDCH!Rat")))
//         {
//             emit logMessage(d_errorString,LogHandler::Error);
//             emit hardwareFailure();
//             return;
//         }

//         if(!mksWrite(QString("PID!ON")))
//         {
//             if(!mksWrite(QString("PID!ON")))
//             {
//                 emit logMessage(d_errorString,LogHandler::Error);
//                 emit hardwareFailure();
//                 return;
//             }
//         }
//     }
//     else
//     {
//         if(!mksWrite(QString("PID!OFF")))
//         {
//             emit logMessage(d_errorString,LogHandler::Error);
//             emit hardwareFailure();
//             return;
//         }
//     }
// }

void Mks946::hwSetPressureControlMode(bool enabled)
{
    if(!isConnected()) { emit logMessage("Cannot set pressure control mode due to a previous communication failure. Reconnect and try again.", LogHandler::Error); return; }

    const int pch = get(pressureChannel,5);
    const QList<QString> chNames{ "A1","A2","B1","B2","C1","C2" };

    if (enabled) {
        QString why;
        if (!ensurePidOff(&why)) { emit logMessage(why, LogHandler::Error); emit hardwareFailure(); return; }

        if(!mksWrite(QString("RPCH!%1").arg(chNames.at(pch-1)))) { emit logMessage(d_errorString,LogHandler::Error); emit hardwareFailure(); return; }
        if(!mksWrite("RDCH!Rat"))                                 { emit logMessage(d_errorString,LogHandler::Error); emit hardwareFailure(); return; }

        if (!ensurePidOn(&why)) { emit logMessage(why, LogHandler::Error); emit hardwareFailure(); return; }
    } else {
        QString why;
        if (!ensurePidOff(&why)) { emit logMessage(why, LogHandler::Error); emit hardwareFailure(); return; }
    }
}



int Mks946::hwReadPressureControlMode()
{
    if(!isConnected())
    {
        return -1;
    }
    QByteArray resp = mksQuery(QString("PID?"));
    if(resp.contains(QByteArray("ON")))
        return 1;
    else if(resp.contains(QByteArray("OFF")))
        return 0;
    else
        emit logMessage(QString("Received invalid response to pressure control mode query. Response: %1").arg(QString(resp)),LogHandler::Error);

    return -1;

}

void Mks946::poll()
{
    if(d_nextRead < 0 || d_nextRead >= get(BC::Key::Flow::flowChannels,4))
    {
        readPressure();
//        readPressureSetpoint();
        d_nextRead = 0;
    }
    else
    {
        readFlow(d_nextRead);
//        readFlowSetpoint(d_nextRead);
        d_nextRead++;
    }
}

void Mks946::fcInitialize()
{
    p_comm->setReadOptions(400,true,QByteArray(";FF"));
    // fcInitialize();
    fcTestConnection();       //GW
}

// bool Mks946::mksWrite(QString cmd)
// {
//     bool pidActive = false;
//     int a = get(address,253); // GW
//     QByteArray resp = p_comm->queryCmd(QString("@%1%2;FF").arg(a,3,10,QChar('0')).arg(cmd));
//     if(resp.contains(QByteArray("ACK")))
//         return true;

//     if(resp.contains(QByteArray("166")))
//     {
//         pidActive = true;
//         if(!mksWrite(QString("PID!OFF")))
//         {
//             emit logMessage(QString("Could not disable PID mode to change pressure setpoint. Error: %1").arg(d_errorString));
//             emit hardwareFailure();
//             return false;	//GW
//         }
//         else
//         {
//             d_errorString = QString("Received invalid response to command %1. Response: %2").arg(cmd).arg(QString(resp));
//             return false;
//         }
//     }
//     return true;
// }

bool Mks946::mksWrite(QString cmd)
{
    const int a = get(address,253);
    auto send = [&](const QString& c) {
        return p_comm->queryCmd(QString("@%1%2;FF").arg(a,3,10,QChar('0')).arg(c));
    };

    QByteArray resp = send(cmd);
    if (resp.contains("ACK"))
        return true;

    // If PID is ON (166), turn it OFF and retry the original command
    if (resp.contains("166")) {
        QString why;
        if (!ensurePidOff(&why)) {
            d_errorString = why.isEmpty()
            ? QString("Failed to turn PID OFF to run '%1'.").arg(cmd)
            : why;
            return false;
        }
        resp = send(cmd);
        if (resp.contains("ACK"))
            return true;
    }

    // Special case: if we were trying to turn PID OFF and got 169, recheck state
    if (cmd == "PID!OFF" && resp.contains("169")) {
        QByteArray q = send("PID?");
        if (q.contains("OFF"))
            return true;  // treat as already-off / benign
    }

    d_errorString = QString("Received invalid response to command %1. Response: %2")
                        .arg(cmd, QString(resp));
    return false;
}


QByteArray Mks946::mksQuery(QString cmd)
{
    int a = get(address,253);
    QByteArray resp = p_comm->queryCmd(QString("@%1%2;FF").arg(a,3,10,QChar('0')).arg(cmd));

    if(!resp.startsWith(QString("@%1ACK").arg(a,3,10,QChar('0')).toLatin1()))
        return resp;

    //chop off prefix
    return resp.mid(7);
}

void Mks946::sleep(bool b)
{
    if(b)
        setPressureControlMode(false);
}
