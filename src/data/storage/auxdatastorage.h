#ifndef AUXDATASTORAGE_H
#define AUXDATASTORAGE_H

#include <set>
#include <map>
#include <vector>

#include <QString>
#include <QVariant>
#include <QDateTime>

namespace  BC::Aux {
static const QString keyTemplate{"%1.%2"};
static const QString hwKeyTemplate{"%1.%2.%3"};
}

class AuxDataStorage
{
public:
    using AuxDataMap = std::map<QString,QVariant>;

    struct TimePointData {
        QDateTime dateTime;
        AuxDataMap map;
    };

    static inline QString makeKey(const QString s1, const QString s2, const QString s3 = "") {
        return s3.isEmpty() ? BC::Aux::keyTemplate.arg(s1).arg(s2) : BC::Aux::hwKeyTemplate.arg(s1).arg(s2).arg(s3);
    }
    int d_number{-1};

    AuxDataStorage() {}
    AuxDataStorage(int number, const QString path="");

    void registerKey(const QString objKey, const QString key);
    void registerKey(const QString hwKey, const QString hwSubKey, const QString key);

    void addDataPoints(AuxDataMap &m);

    void startNewPoint();

    QDateTime currentPointTime() const { return d_currentPoint.dateTime; }

private:
    const QString d_path{""};

    std::set<QString> d_allowedKeys;
    TimePointData d_currentPoint;
    QDateTime d_startTime;


};

Q_DECLARE_METATYPE(AuxDataStorage::AuxDataMap)

#endif // AUXDATASTORAGE_H
