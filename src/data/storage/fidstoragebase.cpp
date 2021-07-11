#include "fidstoragebase.h"

#include <QSaveFile>
#include <QDir>
#include <data/storage/blackchirpcsv.h>
#include <QtConcurrent/QtConcurrent>

FidStorageBase::FidStorageBase(int numRecords, int number, QString path) :
    d_number(number), d_numRecords(numRecords), d_path(path)
{
}

FidStorageBase::~FidStorageBase()
{
}

void FidStorageBase::advance()
{
    save();
    _advance();
}

void FidStorageBase::save()
{
    //if path isn't set, then data can't be saved
    //Don't throw an error; this is probably intentional (peak up mode)
    if(d_number < 1)
        return;

    auto l = getCurrentFidList();
    auto i = getCurrentIndex();

    saveFidList(l,i);
}

void FidStorageBase::saveFidList(const FidList l, int i)
{
    if(d_number < 1)
        return;

    auto f = l.constFirst();
    f.setData({});

    if(i >= d_templateList.size())
        d_templateList.push_back(f);
    else
        d_templateList[i] = f;

    auto tl = d_templateList;

    QDir d{BlackchirpCSV::exptDir(d_number,d_path)};
    if(!d.cd(BC::CSV::fidDir))
    {
        if(!d.mkpath(d.absolutePath()))
            return;
    }

    //do actual writing in another thread
    QtConcurrent::run([l,i,tl,d](){
        BlackchirpCSV csv;
        {
            QSaveFile hdr(d.absoluteFilePath(BC::CSV::fidparams));
            if(!hdr.open(QIODevice::WriteOnly|QIODevice::Text))
                return;

            QTextStream txt(&hdr);
            csv.writeLine(txt,{"index","spacing","probefreq","vmult","shots","sideband","size"});
            for(int idx=0; idx<tl.size(); ++idx)
            {
                auto &f = tl.at(idx);
                csv.writeLine(txt,{idx,f.spacing(),
                                   f.probeFreq(),f.vMult(),f.shots(),f.sideband(),f.size()});
            }
            if(!hdr.commit())
                return;
        }

        {
            QSaveFile dat(d.absoluteFilePath("%1.csv").arg(i));
            if(!dat.open(QIODevice::WriteOnly|QIODevice::Text))
                return;

            csv.writeFidList(dat,l);

            if(!dat.commit())
                return;
        }
    });

}

FidList FidStorageBase::loadFidList(int i)
{
    FidList out;

    QDir d{BlackchirpCSV::exptDir(d_number,d_path)};
    d.cd(BC::CSV::fidDir);

    QFile hdr(d.absoluteFilePath(BC::CSV::fidparams));
    if(!hdr.open(QIODevice::ReadOnly|QIODevice::Text))
        return out;

    Fid fidTemplate;
    bool found = false;
    int size = 0;
    while(!hdr.atEnd())
    {
        auto l = BlackchirpCSV::readLine(hdr);
        if(l.size() != 7)
            continue;

        bool ok = false;
        int idx = l.constFirst().toInt(&ok);
        if(ok)
        {
            if(idx != i)
                continue;

            found = true;
            fidTemplate.setSpacing(l.at(1).toDouble());
            fidTemplate.setProbeFreq(l.at(2).toDouble());
            fidTemplate.setVMult(l.at(3).toDouble());
            fidTemplate.setShots(l.at(4).toULongLong());
            fidTemplate.setSideband(l.at(5).value<RfConfig::Sideband>());
            size = l.at(6).toInt();
        }
    }
    hdr.close();

    if(!found)
        return out;

    QFile fid(d.absoluteFilePath(QString("%1.csv").arg(i)));
    if(!fid.open(QIODevice::ReadOnly|QIODevice::Text))
        return out;

    if(fid.atEnd())
        return out;

    //the first line contains titles, but can be parsed to figure out how many
    //FIDs are in the file
    auto l = BlackchirpCSV::readLine(fid);
    if(l.isEmpty())
        return out;

    QVector<QVector<qint64>> data;
    for(int j=0; j<l.size(); ++j)
    {
        out << fidTemplate;
        QVector<qint64> _d;
        _d.reserve(size);
        data << _d;
    }

    while(!fid.atEnd())
    {
        auto sl = BlackchirpCSV::readFidLine(fid);
        if(sl.size() != data.size())
            continue;

        for(int j=0; j<sl.size(); ++j)
            data[j].append(sl.at(j));
    }

    for(int j=0; j<data.size(); ++j)
        out[j].setData(data.at(j));

    return out;
}

