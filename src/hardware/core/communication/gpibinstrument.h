#ifndef GPIBINSTRUMENT_H
#define GPIBINSTRUMENT_H

#include "communicationprotocol.h"
#include "gpibcontroller.h"

class GpibInstrument : public CommunicationProtocol
{
	Q_OBJECT
public:
	explicit GpibInstrument(QString key, QString subKey, GpibController *c, QObject *parent = nullptr);
	~GpibInstrument();
	void setAddress(int a);
	int address() const;

protected:
	GpibController *p_controller;
	int d_address;

	// CommunicationProtocol interface
public:
    bool writeCmd(QString cmd) override;
    bool writeBinary(QByteArray dat) override;
    QByteArray queryCmd(QString cmd, bool suppressError=false) override;

public slots:
    void initialize() override;
    bool testConnection() override;
};

#endif // GPIBINSTRUMENT_H
