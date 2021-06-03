#include "liflaser.h"

LifLaser::LifLaser(QObject *parent) : HardwareObject (parent)
{
    d_key = QString("lifLaser");
}

LifLaser::~LifLaser()
{

}

double LifLaser::readPosition()
{
    double out = readPos();
    if(out < 0.0)
    {
        emit logMessage(QString("Could not read position.").arg(d_prettyName),BlackChirp::LogError);
        emit hardwareFailure();
    }
    else {
        emit laserPosUpdate(out);
    }

    return out;
}

double LifLaser::setPosition(const double pos)
{
    if(pos < d_minPos || pos > d_maxPos)
    {
        emit logMessage(QString("Requested position (%1 %2) is outside the allowed range of %3 %2 - %4 %2.").arg(pos,0,'f',d_decimals).arg(d_units).arg(d_minPos,0,'f',d_decimals).arg(d_maxPos,0,'f',d_decimals),BlackChirp::LogError);
        emit hardwareFailure();
        return -1.0;
    }

    setPos(pos);

    return readPosition();
}
