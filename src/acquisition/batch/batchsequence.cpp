#include "batchsequence.h"

#include <QDir>

BatchSequence::BatchSequence() :
    BatchManager(BatchManager::Sequence), d_experimentCount(0), d_numExperiments(1), d_intervalSeconds(60), d_autoExport(false), d_waiting(false)
{
    p_intervalTimer = new QTimer(this);
    p_intervalTimer->setSingleShot(true);
    connect(p_intervalTimer,&QTimer::timeout,this,[=](){ d_waiting = false; emit beginExperiment(nextExperiment()); });

    d_exportPath = BlackChirp::getExportDir();
}



void BatchSequence::abort()
{
    if(d_waiting)
    {
        p_intervalTimer->stop();
        p_intervalTimer->blockSignals(true);
        emit batchComplete(true);
        return;
    }
}

void BatchSequence::writeReport()
{
    //not generating reports for this
}

void BatchSequence::processExperiment(const Experiment exp)
{
    //if user wants to export, do that here
    if(d_autoExport)
    {
        QString fileName = d_exportPath + QString("/expt%1.txt").arg(exp.number());
        QFile f(fileName);
        if(f.open(QIODevice::WriteOnly))
        {
            f.close();
            exp.exportAscii(fileName);
            emit logMessage(QString("Exported experiment %1 to %2.").arg(exp.number()).arg(fileName));
        }
        else
            emit logMessage(QString("Could not export experiment %1 to %2.").arg(exp.number()).arg(fileName),BlackChirp::LogWarning);
    }
}

Experiment BatchSequence::nextExperiment()
{
    return d_exp;
}

bool BatchSequence::isComplete()
{
    return d_experimentCount >= d_numExperiments;
}

void BatchSequence::beginNextExperiment()
{
    //start a timer, and emit the beginExperiment signal when done
    //set waiting to true so that this can be aborted if necessary
    if(d_experimentCount == 0)
    {
        emit beginExperiment(d_exp);
    }
    else if(d_experimentCount < d_numExperiments)
    {
        d_waiting = true;
        p_intervalTimer->start(d_intervalSeconds*1000);
        emit statusMessage(QString("Next experiment will start at %1").arg(QDateTime::currentDateTime().addSecs(d_intervalSeconds).toString()));
    }

    d_experimentCount++;
}
