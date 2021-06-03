#ifndef FID_H
#define FID_H

#include <QSharedDataPointer>

#include <QVector>
#include <QPointF>
#include <QMetaType>
#include <QDataStream>

#include "datastructs.h"

class FidData;

/*!
 \brief Implicitly shared data storage for FIDs

 Data are stored in an implicitly shared FidData pointer, which handles reference counting and data storage.
 Please see Qt documentation on implicit sharing for details.
 Basically, this allows for fast copying of data because instead of making a new copy of all the data, a pointer to an existing structure is stored.
 A deep member-by-member copy only occurs if the data is modified.

 An Fid object has a data vector of voltage as a function of time, a probe frequency, and a spacing between points in the vector.
 This class provides a wrapper around the data storage that allows the implicit sharing functionality to work.
 All non-const functions will cause a deep copy if there are multiple references to the Fid object, but const functions will never cause a deep copy to occur.

*/
class Fid
{
public:
    /*!
     \brief Default constructor.

     Allocates a default FidData object.
     */
    Fid();
/*!
 \brief Copy constructor

 \param Fid FID to copy
*/
    Fid(const Fid &);
/*!
 \brief Constructor with explicit initialization

 \param sp Spacing (s)
 \param p Probe frequency (MHz)
 \param d Data vector
*/
    Fid(const double sp, const double p, const QVector<qint64> d, BlackChirp::Sideband sb = BlackChirp::UpperSideband, double vMult = 1.0, qint64 shots = 1);


/*!
     \brief Assignment operator (calls copy constructor)

     \param Fid Fid to copy
     \return Fid Newly-constructed Fid object
    */
    Fid &operator=(const Fid &);
    /*!
     \brief Destructor.

     Explicit declaration is required for implicit sharing functionality

    */
    ~Fid();

    /*!
     \brief Sets spacing (can cause deep copy)

     \param s New spacing (s)
    */
    void setSpacing(const double s);
    /*!
     \brief Sets probe frequency (can cause deep copy)

     \param f New probe frequency (MHz)
    */
    void setProbeFreq(const double f);
    /*!
     \brief Sets data vectoy (can cause deep copy)

     \param d New data vector
    */
    void setData(const QVector<qint64> d);

    void setSideband(const BlackChirp::Sideband sb);

    void setVMult(const double vm);

    void setShots(const qint64 s);

    Fid &operator +=(const Fid other);
    Fid &operator +=(const QVector<qint64> other);
    Fid &operator +=(const qint64 *other);
    Fid &operator -=(const Fid other);

    void add(const qint64 *other, const unsigned int offset = 0);
    void add(const Fid other, int shift);
    void copyAdd(const qint64 *other, const unsigned int offset = 0);
    void rollingAverage(const Fid other, qint64 targetShots, int shift = 0);

    /*!
     \brief Number of points in data vector

     \return int Number of points
    */
    int size() const;

    bool isEmpty() const;
    /*!
     \brief Get voltage at specified index

     \param i Index
     \return double Voltage
    */
    double at(const int i) const;
    /*!
     \brief Accessor function for spacing

     \return double Spacing (s)
    */
    double spacing() const;
    /*!
     \brief Accessor function for probe frequency

     \return double Probe frequency (MHz)
    */
    double probeFreq() const;
    /*!
     \brief Convenience function for making an XY data vector.

     Each point i contains values x = i*spacing and y = voltage at index i

     \return QVector<QPointF> Fid as XY data
    */
    QVector<QPointF> toXY() const;
    /*!
     \brief Accessor function for data vector

     \return QVector<double> Data vector
    */
    QVector<double> toVector() const;

    QVector<qint64> rawData() const;

    qint64 atRaw(const int i) const;

    double atNorm(const int i) const;

    qint64 shots() const;

    BlackChirp::Sideband sideband() const;

    /*!
     * \brief Calculates max frequency of FT from the probe frequency and spacing
     * \return Max frequency, in MHz
     */
    double maxFreq() const;

    double minFreq() const;

    double vMult() const;

    static QByteArray magicString();

private:
    QSharedDataPointer<FidData> data; /*!< The internal data storage object */
};

//DataStream operators
QDataStream &operator<<(QDataStream &stream, const Fid fid);
QDataStream &operator>>(QDataStream &stream, Fid &fid);

/*!
 \brief Internal data for Fid

 Stores the time spacing between points (in s), the probe frequency (in MHz), and the FID data (arbitrary units)
*/
class FidData : public QSharedData {
public:
/*!
 \brief Default constructor

*/
    FidData() : spacing(5e-7), probeFreq(0.0), vMult(1.0), shots(1), fid(QVector<qint64>(0)), sideband(BlackChirp::UpperSideband) {}

    double spacing;
    double probeFreq;
    double vMult;
    qint64 shots;
    QVector<qint64> fid;
    BlackChirp::Sideband sideband;
};

Q_DECLARE_METATYPE(Fid)
typedef QList<Fid> FidList;
Q_DECLARE_TYPEINFO(FidList,Q_MOVABLE_TYPE);

#endif // FID_H
