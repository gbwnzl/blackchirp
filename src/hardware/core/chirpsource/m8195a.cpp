#include "m8195a.h"

#include <math.h>

M8195A::M8195A(QObject *parent) : AWG(parent)
{
    d_subKey = QString("m8195a");
    d_prettyName = QString("Arbitrary Waveform Generator M8195A");
    d_commType = CommunicationProtocol::Tcp;
    d_threaded = false;
}

void M8195A::readSettings()
{
    QSettings s(QSettings::SystemScope,QApplication::organizationName(),QApplication::applicationName());
    s.beginGroup(d_key);
    s.beginGroup(d_subKey);
    double awgRate = s.value(QString("sampleRate"),65e9).toDouble();
    double awgMaxSamples = s.value(QString("maxSamples"),2e9).toDouble();
    double awgMinFreq = s.value(QString("minFreq"),0.0).toDouble();
    double awgMaxFreq = s.value(QString("maxFreq"),26500.0).toDouble();
    bool async = s.value(QString("asyncTrig"),true).toBool();
    bool pp = s.value(QString("hasProtectionPulse"),true).toBool();
    bool ep = s.value(QString("hasAmpEnablePulse"),true).toBool();
    bool ro = s.value(QString("rampOnly"),false).toBool();
    bool triggered = s.value(QString("triggered"),true).toBool();
    s.setValue(QString("sampleRate"),awgRate);
    s.setValue(QString("maxSmaples"),awgMaxSamples);
    s.setValue(QString("minFreq"),awgMinFreq);
    s.setValue(QString("maxFreq"),awgMaxFreq);
    s.setValue(QString("hasProtectionPulse"),pp);
    s.setValue(QString("hasAmpEnablePulse"),ep);
    s.setValue(QString("rampOnly"),ro);
    s.setValue(QString("triggered"),triggered);
    s.setValue(QString("asyncTrig"),async);
    s.endGroup();
    s.endGroup();
    s.sync();
}



bool M8195A::testConnection()
{

    QByteArray resp = p_comm->queryCmd(QString("*IDN?\n"));

    if(resp.isEmpty())
    {
        d_errorString = QString("Did not respond to ID query.");
        return false;
    }

    if(!resp.startsWith(QByteArray("Keysight Technologies,M8195A")))
    {
        d_errorString = QString("ID response invalid. Response: %1 (Hex: %2)").arg(QString(resp.trimmed())).arg(QString(resp.toHex()));
        return false;
    }

    emit logMessage(QString("ID response: %1").arg(QString(resp.trimmed())));

    p_comm->writeCmd(QString("*CLS\n"));

    return true;
}

void M8195A::initialize()
{
    p_comm->setReadOptions(10000,true,QByteArray("\n"));
}

Experiment M8195A::prepareForExperiment(Experiment exp)
{
    d_enabledForExperiment = exp.ftmwConfig().isEnabled();
    if(!d_enabledForExperiment)
        return exp;

    p_comm->writeCmd(QString("*CLS;*RST\n"));
    if(!m8195aWrite(QString(":INST:DACM Marker;:INST:MEM:EXT:RDIV DIV1;:TRAC1:MMOD EXT\n")))
    {
        exp.setErrorString(QString("Could not initialize instrument settings."));
        return exp;
    }

    //external reference (TODO: interface with more general clock system?)
    if(!m8195aWrite(QString(":ROSC:SOUR EXT;:ROSC:FREQ 10000000\n")))
    {
        exp.setErrorString(QString("Could not set to external reference."));
        return exp;
    }

    //external triggering
    QSettings s(QSettings::SystemScope,QApplication::organizationName(),QApplication::applicationName());
    s.beginGroup(d_key);
    s.beginGroup(d_subKey);
    bool triggered = s.value(QString("triggered"),true).toBool();
    double samplerate = s.value(QString("sampleRate"),65e9).toDouble();
    bool async = s.value(QString("asyncTrig"),true).toBool();
    s.endGroup();
    s.endGroup();

    if(triggered)
    {
        QString trig("SYNC");
        if(async)
            trig.prepend(QString("A"));

        if(!m8195aWrite(QString(":INIT:CONT 0;:INIT:GATE 0;:ARM:TRIG:SOUR TRIG;:TRIG:SOUR:ENAB TRIG;:ARM:TRIG:LEV 1.5;:ARM:TRIG:SLOP POS;:ARM:TRIG:OPER %1\n").arg(trig)))
        {
            exp.setErrorString(QString("Could not initialize trigger settings."));
            return exp;
        }
    }
    else
    {
        if(!m8195aWrite(QString(":INIT:CONT 1;:INIT:GATE 0\n")))
        {
            exp.setErrorString(QString("Could not initialize continuous signal generation."));
            return exp;
        }
    }

    if(!m8195aWrite(QString(":VOLTAGE 1.0\n")))
    {
        exp.setErrorString(QString("Could not set output voltage."));
        return exp;
    }

    if(!m8195aWrite(QString(":FREQ:RAST %1\n").arg(samplerate,0,'E',1)))
    {
        exp.setErrorString(QString("Could not set sample rate."));
        return exp;
    }

    if(!m8195aWrite(QString(":TRAC:DEL:ALL\n")))
    {
        exp.setErrorString(QString("Could not delete old traces."));
        return exp;
    }

    auto data = exp.ftmwConfig().chirpConfig().getChirpMicroseconds();
    auto markerData = exp.ftmwConfig().chirpConfig().getMarkerData();

    if(data.size() != markerData.size())
    {
        exp.setErrorString(QString("Waveform and marker data are not same length. This is a bug; please report it."));
        exp.setHardwareFailed();
        return exp;
    }

    int len = data.size() + (data.size()%256);

    QByteArray id = p_comm->queryCmd(QString(":TRAC1:DEF:NEW? %1\n").arg(len)).trimmed();
    if(id.isEmpty())
    {
        exp.setErrorString(QString("Could not create new AWG trace."));
        exp.setHardwareFailed();
        return exp;
    }

    //each transfer must align with 256-sample memory vectors
    int chunkSize = 1 << 20;
    int chunks = static_cast<int>(ceil(static_cast<double>(data.size())/static_cast<double>(chunkSize)));
    int currentChunk = 0;

    QByteArray chunkData;
    chunkData.reserve(chunkSize*2);
    bool success = true;

    while(currentChunk < chunks)
    {
        chunkData.clear();
        int startIndex = currentChunk*chunkSize;
        //if this chunk runs past the data size, pad with zeros until we reach nearest
        //multiple of 256
        int endIndex = qMin((currentChunk+1)*chunkSize,data.size()+(data.size()%256));
        int numPnts = endIndex - startIndex;

        //AWG has analog and marker values interleaved
        for(int i=0; i < numPnts; i++)
        {
            qint8 low = -127;
            qint8 high = 127;
            //convert doubles to qint8: -1.0 --> -127, +1.0 --> 127
            qint8 chirpVal = 0;
            if(startIndex + i < data.size())
                 chirpVal = qBound(low,static_cast<qint8>(round(data.at(startIndex+i).y()*127.0)),high);

            //markers are binary switches: marker 1 (ch 3) is bit 0; marker 2 (ch 4) is bit 1
            qint8 markerVal = 0;

            if(startIndex + i < data.size())
            {
                if(markerData.at(startIndex+i).first) //marker 1 (protection)
                    markerVal++;
                if(markerData.at(startIndex+i).second) //marker 2 (amp gate)
                    markerVal+=2;
            }

            chunkData.append(chirpVal).append(markerVal);
        }

        //create data header
        QString header = QString(":TRAC1:DATA %1,%2,")
                .arg(QString(id)).arg(startIndex);

        QString binSize = QString::number(numPnts*2);
        QString binHeader = QString("#%1%2").arg(binSize.size()).arg(binSize);
        header.append(binHeader);

        if(!p_comm->writeCmd(header))
        {
            success = false;
            exp.setErrorString(QString("Could not write header data to AWG. Header: %1").arg(header));
            break;
        }

        if(!p_comm->writeBinary(chunkData))
        {
            success = false;
            exp.setErrorString(QString("!Could not write waveform data to AWG. Header was: %1").arg(header));
            break;
        }

        p_comm->writeCmd(QString("\n"));


        QByteArray resp = p_comm->queryCmd(QString("SYST:ERR?\n"));
        if(!resp.startsWith('0'))
        {
            exp.setErrorString(QString("Could not write waveform data to AWG. Error %1. Header was: %2").arg(QString(resp)).arg(header));
            success = false;
            break;
        }

        currentChunk++;
    }

    if(!success)
        exp.setHardwareFailed();

    return exp;

}

void M8195A::beginAcquisition()
{
    if(d_enabledForExperiment)
    {
        p_comm->writeCmd(QString(":OUTP1 1;:OUTP3 1;:OUTP4 1\n"));
        p_comm->writeCmd(QString(":INIT:IMM\n"));
    }
}

void M8195A::endAcquisition()
{
    if(d_enabledForExperiment)
    {
        p_comm->writeCmd(QString(":OUTP1 0;:OUTP3 0;:OUTP4 0\n"));
        p_comm->writeCmd(QString(":ABOR\n"));
    }
}

bool M8195A::m8195aWrite(const QString cmd)
{
    if(!p_comm->writeCmd(cmd))
        return false;

    QByteArray resp = p_comm->queryCmd(QString("SYST:ERR?\n"));
    if(!resp.startsWith('0'))
    {
       emit logMessage(QString("Could not write waveform data to AWG. Error %1. Command was: %2").arg(QString(resp)).arg(cmd),BlackChirp::LogError);
        return false;
    }

    return true;
}
