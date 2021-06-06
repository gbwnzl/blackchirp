#ifndef AWG7122B_H
#define AWG7122B_H

#include <src/hardware/core/chirpsource/awg.h>

#include <src/data/experiment/chirpconfig.h>

class AWG7122B : public AWG
{
    Q_OBJECT
public:
    explicit AWG7122B(QObject *parent = nullptr);

    // HardwareObject interface
public slots:
    void readSettings() override;
    bool prepareForExperiment(Experiment &exp) override;
    void beginAcquisition() override;
    void endAcquisition() override;

protected:
    bool testConnection() override;
    void initialize() override;


private:
    QString getWaveformKey(const ChirpConfig cc);
    QString writeWaveform(const ChirpConfig cc);

    bool d_triggered;
};

#endif // AWG7122B_H
