#include "experiment.h"

#include <QSettings>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

Experiment::Experiment() : data(new ExperimentData)
{

}

Experiment::Experiment(const Experiment &rhs) : data(rhs.data)
{

}

Experiment &Experiment::operator=(const Experiment &rhs)
{
    if (this != &rhs)
        data.operator=(rhs.data);
    return *this;
}

Experiment::Experiment(const int num, QString exptPath) : data(new ExperimentData)
{
    QDir d(BlackChirp::getExptDir(num,exptPath));
    if(!d.exists())
        return;

    data->iobCfg = IOBoardConfig(false);

    QFile hdr(BlackChirp::getExptFile(num,BlackChirp::HeaderFile,exptPath));
    if(hdr.open(QIODevice::ReadOnly))
    {
        while(!hdr.atEnd())
        {
            QString line = QString(hdr.readLine());
            if(line.isEmpty())
                continue;

            QStringList l = line.split(QString("\t"));
            if(l.size() < 2)
                continue;

            QString key = l.first();
            QVariant val = QVariant(l.at(1));

            if(key.startsWith(QString("Ftmw")))
                data->ftmwCfg.parseLine(key,val);

            if(key.startsWith(QString("Flow")))
                data->flowCfg.parseLine(key,val);

            if(key.startsWith(QString("IOBoardConfig")))
                data->iobCfg.parseLine(key,val);

            if(key.startsWith(QString("PulseGen")))
                data->pGenCfg.parseLine(key,val);

            if(key.startsWith(QString("Lif")))
                data->lifCfg.parseLine(key,val);
        }

        hdr.close();

        if(data->ftmwCfg.isEnabled())
        {
            data->ftmwCfg.loadChirps(num,exptPath);
            data->ftmwCfg.loadFids(num,exptPath);
        }

        if(data->lifCfg.isEnabled())
            data->lifCfg.loadLifData(num,exptPath);
    }
    else
    {
        data->errorString = QString("Could not open header file (%1)").arg(hdr.fileName());
        return;
    }

    //load time data
    QFile tdt(BlackChirp::getExptFile(num,BlackChirp::TimeFile,exptPath));
    if(tdt.open(QIODevice::ReadOnly))
    {
        bool plot = true;
        bool lookForHeader = true;
        QStringList hdrList;

        while(!tdt.atEnd())
        {
            QByteArray line = tdt.readLine().trimmed();

            if(line.isEmpty())
                continue;

            if(line.startsWith('#'))
            {
                if(line.endsWith("NoPlotData"))
                {
                    plot = false;
                    lookForHeader = true;
                    hdrList.clear();
                    continue;
                }
                else if(line.endsWith("PlotData"))
                {
                    plot = true;
                    lookForHeader = true;
                    hdrList.clear();
                    continue;
                }
                else
                    continue;
            }

            QByteArrayList l = line.split('\t');
            if(l.isEmpty())
                continue;

            if(lookForHeader)
            {
                for(int i=0; i<l.size(); i++)
                {
                    QByteArrayList l2 = l.at(i).split('_');
                    QString name;
                    for(int j=0; j<l2.size()-1; j++)
                        name += QString(l2.at(j));

                    hdrList.append(name);
                    data->timeDataMap[name] = qMakePair(QList<QVariant>(),plot);
                }
                lookForHeader = false;
            }
            else
            {
                if(l.size() != hdrList.size())
                    continue;

                for(int i=0; i<l.size(); i++)
                    data->timeDataMap[hdrList.at(i)].first.append(QString(l.at(i).trimmed()));
            }

        }

        tdt.close();
    }


    data->number = num;

}

Experiment::~Experiment()
{

}

int Experiment::number() const
{
    return data->number;
}

QDateTime Experiment::startTime() const
{
    return data->startTime;
}

int Experiment::timeDataInterval() const
{
    return data->timeDataInterval;
}

int Experiment::autoSaveShots() const
{
    return data->autoSaveShotsInterval;
}

bool Experiment::isInitialized() const
{
    return data->isInitialized;
}

bool Experiment::isAborted() const
{
    return data->isAborted;
}

bool Experiment::isDummy() const
{
    return data->isDummy;
}

bool Experiment::isLifWaiting() const
{
    return data->waitForLifSet;
}

FtmwConfig Experiment::ftmwConfig() const
{
    return data->ftmwCfg;
}

PulseGenConfig Experiment::pGenConfig() const
{
    return data->pGenCfg;
}

FlowConfig Experiment::flowConfig() const
{
    return data->flowCfg;
}

LifConfig Experiment::lifConfig() const
{
    return data->lifCfg;
}

IOBoardConfig Experiment::iobConfig() const
{
    return data->iobCfg;
}

bool Experiment::isComplete() const
{
    //check each sub expriment!
    return (data->ftmwCfg.isComplete() && data->lifCfg.isComplete());
}

bool Experiment::hardwareSuccess() const
{
    return data->hardwareSuccess;
}

QString Experiment::errorString() const
{
    return data->errorString;
}

QMap<QString, QPair<QList<QVariant>, bool> > Experiment::timeDataMap() const
{
    return data->timeDataMap;
}

QString Experiment::startLogMessage() const
{
    return data->startLogMessage;
}

QString Experiment::endLogMessage() const
{
    return data->endLogMessage;
}

BlackChirp::LogMessageCode Experiment::endLogMessageCode() const
{
    return data->endLogMessageCode;
}

QMap<QString, QPair<QVariant, QString> > Experiment::headerMap() const
{
    QMap<QString, QPair<QVariant, QString> > out;

    out.insert(QString("AuxDataInterval"),qMakePair(data->timeDataInterval,QString("s")));
    out.insert(QString("AutosaveInterval"),qMakePair(data->autoSaveShotsInterval,QString("shots")));

    auto it = data->validationConditions.constBegin();
    QString prefix("Validation.");
    QString empty("");
    for(;it != data->validationConditions.constEnd(); it++)
    {
        out.insert(prefix+it.key()+QString(".Min"),qMakePair(it.value().min,empty));
        out.insert(prefix+it.key()+QString(".Max"),qMakePair(it.value().max,empty));
    }

    out.unite(data->ftmwCfg.headerMap());
    out.unite(data->lifCfg.headerMap());
    out.unite(data->pGenCfg.headerMap());
    out.unite(data->flowCfg.headerMap());
    out.unite(data->iobCfg.headerMap());

    return out;
}

bool Experiment::snapshotReady()
{
    if(isComplete())
        return false;

    if(ftmwConfig().isEnabled())
    {
        if(ftmwConfig().completedShots() > 0)
        {
            qint64 d = ftmwConfig().completedShots() - data->lastSnapshot;
            if(d > 0)
            {
                bool out = !(d % static_cast<qint64>(data->autoSaveShotsInterval));
                if(out)
                    data->lastSnapshot = ftmwConfig().completedShots();
                return out;
            }
            else
                return false;
        }
    }
    else if(lifConfig().isEnabled())
    {
        if(lifConfig().completedShots() > 0)
        {
            qint64 d = lifConfig().completedShots() - data->lastSnapshot;
            if(d > 0)
            {
                bool out = !(d % static_cast<qint64>(data->autoSaveShotsInterval));
                if(out)
                    data->lastSnapshot = lifConfig().completedShots();
                return out;
            }
            else
                return false;
        }
    }

    return false;
}

void Experiment::setTimeDataInterval(const int t)
{
    data->timeDataInterval = t;
}

void Experiment::setAutoSaveShotsInterval(const int s)
{
    data->autoSaveShotsInterval = s;
}

void Experiment::setInitialized()
{
    bool initSuccess = true;
    data->startTime = QDateTime::currentDateTime();

    if(data->ftmwCfg.isEnabled())
    {
        if(!data->ftmwCfg.prepareForAcquisition())
        {
            setErrorString(data->ftmwCfg.errorString());
            data->isInitialized = false;
            return;
        }
    }

    QSettings s(QSettings::SystemScope,QApplication::organizationName(),QApplication::applicationName());
    int num = s.value(QString("exptNum"),0).toInt()+1;
    num = 1; //REMOVE
    data->number = num;

    if(ftmwConfig().isEnabled() && ftmwConfig().type() == BlackChirp::FtmwPeakUp)
    {
        data->number = -1;
        data->startLogMessage = QString("Peak up mode started.");
        data->endLogMessage = QString("Peak up mode ended.");
        data->isDummy = true;
        data->isInitialized = true;
        return;
    }
    else
    {
        data->startLogMessage = QString("Starting experiment %1.").arg(num);
        data->endLogMessage = QString("Experiment %1 complete.").arg(num);
    }

    QDir d(BlackChirp::getExptDir(num));
    if(!d.exists())
    {
        initSuccess = d.mkpath(d.absolutePath());
        if(!initSuccess)
        {
            data->isInitialized = false;
            data->errorString = QString("Could not create the directory %1 for saving.").arg(d.absolutePath());
            return;
        }
    }

    //write headers; chirps, etc
    //scan header
    if(!saveHeader())
    {
        data->isInitialized = false;
        data->errorString = QString("Could not open the file %1 for writing.")
                .arg(BlackChirp::getExptFile(data->number,BlackChirp::HeaderFile));
        return;
    }

    //chirp file
    if(data->ftmwCfg.isEnabled())
    {
        if(!saveChirpFile())
        {
            data->isInitialized = false;
            data->errorString = QString("Could not open the file %1 for writing.")
                    .arg(BlackChirp::getExptFile(num,BlackChirp::ChirpFile));
            return;
        }
    }


    data->isInitialized = initSuccess;

    if(initSuccess)
        s.setValue(QString("exptNum"),number());

}

void Experiment::setAborted()
{
    data->isAborted = true;
    if(ftmwConfig().isEnabled() && (ftmwConfig().type() == BlackChirp::FtmwTargetShots || ftmwConfig().type() == BlackChirp::FtmwTargetTime ))
    {
        data->endLogMessage = QString("Experiment %1 aborted.").arg(number());
        data->endLogMessageCode = BlackChirp::LogError;
    }
    else if(ftmwConfig().isEnabled() && lifConfig().isEnabled() && !lifConfig().isComplete())
    {
        data->endLogMessage = QString("Experiment %1 aborted.").arg(number());
        data->endLogMessageCode = BlackChirp::LogError;
    }
}

void Experiment::setDummy()
{
    data->isDummy = true;
}

void Experiment::setLifWaiting(bool wait)
{
    data->waitForLifSet = wait;
}

void Experiment::setFtmwConfig(const FtmwConfig cfg)
{
    data->ftmwCfg = cfg;
}

void Experiment::setScopeConfig(const BlackChirp::FtmwScopeConfig &cfg)
{
    data->ftmwCfg.setScopeConfig(cfg);
}

void Experiment::setLifConfig(const LifConfig cfg)
{
    data->lifCfg = cfg;
}

void Experiment::setIOBoardConfig(const IOBoardConfig cfg)
{
    data->iobCfg = cfg;
}

bool Experiment::setFidsData(const QList<QVector<qint64> > l)
{
    if(!data->ftmwCfg.setFidsData(l))
    {
        setErrorString(ftmwConfig().errorString());
        return false;
    }

    return true;
}

bool Experiment::addFids(const QByteArray newData, int shift)
{
    if(!data->ftmwCfg.addFids(newData,shift))
    {
        setErrorString(ftmwConfig().errorString());
        return false;
    }

    return true;
}

bool Experiment::addLifWaveform(const LifTrace t)
{
    return data->lifCfg.addWaveform(t);
}

void Experiment::overrideTargetShots(const int target)
{
    data->ftmwCfg.setTargetShots(target);
}

void Experiment::resetFids()
{
    data->ftmwCfg.resetFids();
}

void Experiment::setPulseGenConfig(const PulseGenConfig c)
{
    data->pGenCfg = c;
}

void Experiment::setFlowConfig(const FlowConfig c)
{
    data->flowCfg = c;
}

void Experiment::setErrorString(const QString str)
{
    data->errorString = str;
}

bool Experiment::addTimeData(const QList<QPair<QString, QVariant> > dataList, bool plot)
{
    //return false if scan should be aborted
    bool out = true;
    for(int i=0; i<dataList.size(); i++)
    {
        QString key = dataList.at(i).first;
        QVariant value = dataList.at(i).second;

        if(data->timeDataMap.contains(key))
            data->timeDataMap[key].first.append(value);
        else
        {
            QList<QVariant> newList;
            newList.append(value);
            data->timeDataMap.insert(key,qMakePair(newList,plot));
        }

        if(data->validationConditions.contains(key))
        {
            //convert key if needed
            QString name = BlackChirp::channelNameLookup(key);
            if(name.isEmpty())
                name = key;

            bool ok = false;
            double d = value.toDouble(&ok);

            if(!ok)
            {
                out = false;
                data->errorString = QString("Aborting because the item \"%1\" (value = %2) cannot be converted to a double.").arg(name).arg(value.toString());
                break;
            }
            else
            {
                const BlackChirp::ValidationItem &vi = data->validationConditions.value(key);
                if(d < vi.min || d > vi.max)
                {
                    out = false;
                    data->errorString = QString("Aborting because %1 is outside specified range (Value = %2, Min = %3, Max = %4).")
                            .arg(name).arg(d,0,'g').arg(vi.min,0,'g').arg(vi.max,0,'g');
                    break;
                }
            }
        }
    }

    return out;
}

void Experiment::addTimeStamp()
{
    QString key("exptTimeStamp");
    if(data->timeDataMap.contains(key))
        data->timeDataMap[key].first.append(QDateTime::currentDateTime());
    else
    {
        QList<QVariant> newList;
        newList.append(QDateTime::currentDateTime());
        data->timeDataMap.insert(key,qMakePair(newList,false));
    }
}

void Experiment::setValidationItems(const QMap<QString, BlackChirp::ValidationItem> m)
{
    data->validationConditions = m;
}

void Experiment::setHardwareFailed()
{
    data->hardwareSuccess = false;
}

void Experiment::incrementFtmw()
{
    data->ftmwCfg.increment();
}

void Experiment::finalSave() const
{
    if(data->isDummy)
        return;

    //record validation keys
    QSettings s(QSettings::SystemScope,QApplication::organizationName(),QApplication::applicationName());
    QString keys = s.value(QString("knownValidationKeys"),QString("")).toString();
    QStringList knownKeyList = keys.split(QChar(';'),QString::SkipEmptyParts);

    auto it = data->timeDataMap.constBegin();
    while(it != data->timeDataMap.constEnd())
    {
        QString key = it.key();
        if(!knownKeyList.contains(key))
            knownKeyList.append(key);
        it++;
    }

    keys.clear();
    if(knownKeyList.size() > 0)
    {
        keys = knownKeyList.at(0);
        for(int i=1; i<knownKeyList.size();i++)
            keys.append(QString(";%1").arg(knownKeyList.at(i)));

        s.setValue(QString("knownValidationKeys"),keys);
    }

    if(ftmwConfig().isEnabled())
        ftmwConfig().writeFidFile(data->number);

    if(lifConfig().isEnabled())
            lifConfig().writeLifFile(data->number);

    saveTimeFile();
}

bool Experiment::saveHeader() const
{
    QFile hdr(BlackChirp::getExptFile(data->number,BlackChirp::HeaderFile));
    if(hdr.open(QIODevice::WriteOnly))
    {
        QTextStream t(&hdr);
        t << BlackChirp::headerMapToString(headerMap());
        t.flush();
        hdr.close();
        return true;
    }
    else
        return false;
}

bool Experiment::saveChirpFile() const
{
    QFile chp(BlackChirp::getExptFile(data->number,BlackChirp::ChirpFile));
    if(chp.open(QIODevice::WriteOnly))
    {
        QTextStream t(&chp);
        t << data->ftmwCfg.chirpConfig().toString();
        t.flush();
        chp.close();
        return true;
    }
    else
        return false;
}

bool Experiment::saveTimeFile() const
{
    if(data->timeDataMap.isEmpty())
        return true;

    QFile tdt(BlackChirp::getExptFile(data->number,BlackChirp::TimeFile));
    if(tdt.open(QIODevice::WriteOnly))
    {
        QList<QPair<QString,QList<QVariant>>> plot, noPlot;
        QTextStream t(&tdt);
        t.setRealNumberNotation(QTextStream::ScientificNotation);
        t.setRealNumberPrecision(6);
        QString tab("\t");
        QString nl("\n");

        auto it = data->timeDataMap.constBegin();
        int maxPlotSize = 0, maxNoPlotSize = 0;
        for(;it != data->timeDataMap.constEnd(); it++)
        {
            QString alias = BlackChirp::channelNameLookup(it.key());
            if(!alias.isEmpty())
                t << QString("#Alias") << tab << alias << tab << it.key() << nl;
            bool p = it.value().second;
            if(p)
            {
                plot.append(qMakePair(it.key(),it.value().first));
                maxPlotSize = qMax(it.value().first.size(),maxPlotSize);
            }
            else
            {
                noPlot.append(qMakePair(it.key(),it.value().first));
                maxNoPlotSize = qMax(it.value().first.size(),maxNoPlotSize);
            }
        }


        if(!plot.isEmpty())
        {
            t << QString("#PlotData\n\n");
            QString name = BlackChirp::channelNameLookup(plot.first().first);
            if(name.isEmpty())
                name = plot.first().first;

            t << name << QString("_%1").arg(data->number);
            for(int i=1; i<plot.size(); i++)
            {
                name = BlackChirp::channelNameLookup(plot.at(i).first);
                if(name.isEmpty())
                    name = plot.at(i).first;

                t << tab << name << QString("_%1").arg(data->number);
            }

            for(int i=0; i<maxPlotSize; i++)
            {
                t << nl;

                if(i >= plot.first().second.size())
                    t << QString("NaN");
                else
                {
                    if(plot.first().second.at(i).canConvert(QVariant::Double))
                        t << plot.first().second.at(i).toDouble();
                    else
                        t << plot.first().second.at(i).toString();
                }

                for(int j=1; j<plot.size(); j++)
                {
                    if(i >= plot.at(j).second.size())
                        t << tab << QString("NaN");
                    else
                    {
                        if(plot.at(j).second.at(i).canConvert(QVariant::Double))
                            t << tab << plot.at(j).second.at(i).toDouble();
                        else
                            t << tab << plot.at(j).second.at(i).toString();
                    }
                }
            }
        }

        if(!noPlot.isEmpty())
        {
            t << QString("\n\n#NoPlotData\n\n");
            QString name = BlackChirp::channelNameLookup(noPlot.first().first);
            if(name.isEmpty())
                name = noPlot.first().first;

            t << name << QString("_%1").arg(data->number);
            for(int i=1; i<noPlot.size(); i++)
            {
                name = BlackChirp::channelNameLookup(noPlot.at(i).first);
                if(name.isEmpty())
                    name = noPlot.at(i).first;

                t << tab <<name << QString("_%1").arg(data->number);
            }

            for(int i=0; i<maxNoPlotSize; i++)
            {
                t << nl;

                if(i >= noPlot.first().second.size())
                    t << QString("NaN");
                else
                {
                    if(noPlot.first().second.at(i).canConvert(QVariant::Double))
                        t << noPlot.first().second.at(i).toDouble();
                    else
                        t << noPlot.first().second.at(i).toString();
                }

                for(int j=1; j<noPlot.size(); j++)
                {
                    if(i >= noPlot.at(j).second.size())
                        t << tab << QString("NaN");
                    else
                    {
                        if(noPlot.at(j).second.at(i).canConvert(QVariant::Double))
                            t << tab << noPlot.at(j).second.at(i).toDouble();
                        else
                            t << tab <<noPlot.at(j).second.at(i).toString();
                    }
                }
            }
        }
        t.flush();
        tdt.close();
        return true;
    }
    else
        return false;
}

void Experiment::snapshot(int snapNum, const Experiment other) const
{
    if(data->isDummy)
        return;

    if(ftmwConfig().isEnabled())
    {
        FtmwConfig cf = ftmwConfig();
        if(other.number() == data->number && other.isInitialized())
        {
            if(cf.subtractFids(other.ftmwConfig().fidList()))
                cf.writeFidFile(data->number,snapNum);
        }
        else
            cf.writeFidFile(data->number,snapNum);
    }

    if(lifConfig().isEnabled())
        lifConfig().writeLifFile(data->number);

    saveTimeFile();
}

