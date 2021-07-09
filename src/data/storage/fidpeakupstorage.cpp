#include "fidpeakupstorage.h"

FidPeakUpStorage::FidPeakUpStorage(int numRecords) :
    FidStorageBase(QString(""),numRecords), p_mutex(new QMutex)
{
    d_currentFidList = newFidList();
}

FidPeakUpStorage::~FidPeakUpStorage()
{
    delete p_mutex;
}


quint64 FidPeakUpStorage::completedShots()
{
    QMutexLocker l(p_mutex);
    return d_currentFidList.constFirst().shots();
}

quint64 FidPeakUpStorage::currentSegmentShots()
{
    return completedShots();
}

bool FidPeakUpStorage::addFids(const FidList other, int shift)
{
    QMutexLocker l(p_mutex);
    if(other.size() != d_currentFidList.size())
        return false;

    if(d_targetShots == 1 || d_currentFidList.constFirst().size() == 0)
        d_currentFidList = other;
    else
    {
        for(int i=0; i<d_currentFidList.size(); i++)
            d_currentFidList[i].rollingAverage(other.at(i),d_targetShots,shift);
    }
    return true;
}

FidList FidPeakUpStorage::getFidList(std::size_t i)
{
    Q_UNUSED(i)
    QMutexLocker l(p_mutex);
    return d_currentFidList;
}

FidList FidPeakUpStorage::getCurrentFidList()
{
    QMutexLocker l(p_mutex);
    return d_currentFidList;
}

void FidPeakUpStorage::reset()
{
    QMutexLocker l(p_mutex);
    d_currentFidList = newFidList();
}

int FidPeakUpStorage::getCurrentIndex()
{
    return 0;
}

void FidPeakUpStorage::_advance()
{
}

void FidPeakUpStorage::setTargetShots(quint64 s)
{
    QMutexLocker l(p_mutex);
    d_targetShots = s;
}

#ifdef BC_CUDA
bool FidPeakUpStorage::setFidsData(const FidList other)
{
    QMutexLocker l(p_mutex);
    if(other.size() != d_currentFidList.size())
        return false;

    d_currentFidList = other;
    return true;
}
#endif
