#ifndef M412220X8_H
#define M412220X8_H

#include "ftmwscope.h"

#include "spcm/dlltyp.h"
#include "spcm/regs.h"
#include "spcm/spcerr.h"
#include "spcm/spcm_drv.h"


#include <QTimer>


class M4i2220x8 : public FtmwScope
{
    Q_OBJECT
public:
    explicit M4i2220x8(QObject *parent = nullptr);
    ~M4i2220x8();

    // HardwareObject interface
public slots:
    void readSettings() override;
    Experiment prepareForExperiment(Experiment exp) override;
    void beginAcquisition() override;
    void endAcquisition() override;

    // FtmwScope interface
    void readWaveform() override;

protected:
    bool testConnection() override;
    void initialize() override;

private:
    drv_handle p_handle;

    qint64 d_waveformBytes;
    char *p_m4iBuffer;
    int d_bufferSize;

    QTimer *p_timer;

};

#endif // M412220X8_H
