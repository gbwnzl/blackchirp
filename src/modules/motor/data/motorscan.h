#ifndef MOTORSCAN_H
#define MOTORSCAN_H

#include <QSharedDataPointer>
#include <QVector>
#include <QList>
#include <QPointF>
#include <QVector3D>

#include <qwt6/qwt_matrix_raster_data.h>

#include "datastructs.h"
#include "analysis.h"

class MotorScanData;
class QwtMatrixRasterData;

class MotorScan
{
public:
    MotorScan();
    MotorScan(const MotorScan &rhs);
    MotorScan &operator=(const MotorScan &rhs);

    static MotorScan fromSettings();
    void saveToSettings() const;
    bool isInitialized() const;
    bool hardwareError() const;

    bool isEnabled() const;
    QMap<QString,QPair<QVariant,QString> > headerMap() const;

    int xPoints() const;
    int yPoints() const;
    int zPoints() const;
    int tPoints() const;
    int numPoints(BlackChirp::MotorAxis axis) const;

    double xVal(int i) const;
    double yVal(int i) const;
    double zVal(int i) const;
    double tVal(int i) const;
    double axisValue(BlackChirp::MotorAxis axis, int i) const;
    QPair<double,double> range(BlackChirp::MotorAxis axis) const;
    QPair<double,double> interval(BlackChirp::MotorAxis axis) const;

    double value(int x, int y, int z, int t) const;
    double smoothValue(int x, int y, int z, int t, Eigen::MatrixXd coefs) const;

    bool writeMotorFile(int num) const;

    int shotsPerPoint() const;
    int currentPointShots() const;
    int completedShots() const;
    bool isPointComplete() const;
    bool isComplete() const;
    QVector3D currentPos() const;

    QVector<double> slice(BlackChirp::MotorAxis xAxis, BlackChirp::MotorAxis yAxis, BlackChirp::MotorAxis otherAxis1, int otherPoint1, BlackChirp::MotorAxis otherAxis2, int otherPoint2) const;
    QVector<double> smoothSlice(BlackChirp::MotorAxis xAxis, BlackChirp::MotorAxis yAxis, BlackChirp::MotorAxis otherAxis1, int otherPoint1, BlackChirp::MotorAxis otherAxis2, int otherPoint2, Eigen::MatrixXd coefs) const;
    QVector<QPointF> tTrace(int x, int y, int z) const;
    QVector<QPointF> smoothtTrace(int x, int y, int z, Eigen::MatrixXd coefs) const;
    BlackChirp::MotorScopeConfig scopeConfig() const;

    void parseLine(QString key, QVariant val);
    void setEnabled(bool en = true);

    void setXPoints(int x);
    void setYPoints(int y);
    void setZPoints(int z);
    void setTPoints(int t);
    void setIntervals(double x0, double y0, double z0, double dx, double dy, double dz);

    bool loadMotorData(int num, const QString path);
    void setShotsPerPoint(const int s);
    void setScopeConfig(const BlackChirp::MotorScopeConfig &sc);
    void setHardwareError();
    void initialize();
    bool addTrace(const QVector<double> d);
    void advance();
    void abort();






private:
    QSharedDataPointer<MotorScanData> data;
};

class MotorScanData : public QSharedData
{
public:
    MotorScanData() : enabled(false), xPoints(0), yPoints(0), zPoints(0), tPoints(0),
    x0(0.0), y0(0.0), z0(0.0), t0(0.0), dx(1.0), dy(1.0), dz(1.0), dt(1.0),
      initialized(false), hardwareError(false) {}

    bool enabled;

    int xPoints;
    int yPoints;
    int zPoints;
    int tPoints;



    double x0;
    double y0;
    double z0;
    double t0;

    double dx;
    double dy;
    double dz;
    double dt;

    bool initialized;
    bool hardwareError;
    int shotsPerPoint;
    int currentPointShots;

    int totalPoints;
    int currentPoint;

    int currentX;
    int currentY;
    int currentZ;

    QList<QList<QList<QVector<double>>>> zyxtData;
    BlackChirp::MotorScopeConfig scopeConfig;
};

#endif // MOTORSCAN_H
