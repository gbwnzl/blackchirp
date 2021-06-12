#ifndef VIRTUALPULSEGENERATOR_H
#define VIRTUALPULSEGENERATOR_H

#include <src/hardware/core/pulsegenerator/pulsegenerator.h>

namespace BC::Key {
static const QString vpGen("Virtual Pulse Generator");
}

class VirtualPulseGenerator : public PulseGenerator
{
    Q_OBJECT
public:
    explicit VirtualPulseGenerator(QObject *parent = nullptr);
    ~VirtualPulseGenerator();

    // PulseGenerator interface
    QVariant read(const int index, const BlackChirp::PulseSetting s) override;
    double readRepRate() override;

    bool set(const int index, const BlackChirp::PulseSetting s, const QVariant val) override;
    bool setRepRate(double d) override;

protected:
    bool testConnection() override;
    void initializePGen() override {}
};

#endif // VIRTUALPULSEGENERATOR_H
