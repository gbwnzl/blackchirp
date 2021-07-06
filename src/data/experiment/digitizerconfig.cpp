#include "digitizerconfig.h"

#include <cmath>

DigitizerConfig::DigitizerConfig(const QString key) : HeaderStorage(key)
{

}

double DigitizerConfig::xIncr() const
{
    if(d_sampleRate > 0.0)
        return 1.0/d_sampleRate;

    return nan("");
}

double DigitizerConfig::yMult(int ch) const
{
    auto it = d_analogChannels.find(ch);
    if(it == d_analogChannels.end())
        return nan("");

    return it->second.fullScale * static_cast<double>( 2 << (d_bytesPerPoint*8 - 1));
}


void DigitizerConfig::prepareToSave()
{
    using namespace BC::Store::Digi;
    for(auto it = d_analogChannels.cbegin(); it != d_analogChannels.cend(); ++it)
    {
        storeArrayValue(an,it->first,en,true);
        storeArrayValue(an,it->first,fs,it->second.fullScale,"V");
        storeArrayValue(an,it->first,offset,it->second.offset,"V");
    }
    for(auto it = d_digitalChannels.cbegin(); it != d_digitalChannels.cend(); ++it)
    {
        storeArrayValue(dig,it->first,en,true);
    }

    store(trigCh,d_triggerChannel);
    store(trigSlope,d_triggerSlope);
    store(trigLevel,d_triggerLevel,"V");
    store(trigDelay,d_triggerDelayUSec,QString::fromUtf8("μs"));
    store(bpp,d_bytesPerPoint);
    store(bo,d_byteOrder);
    store(sRate,d_sampleRate,"Hz");
    store(recLen,d_recordLength);
    store(blockAvg,d_blockAverage);
    store(numAvg,d_numAverages);
    store(multiRec,d_multiRecord);
    store(multiRecNum,d_numRecords);
}

void DigitizerConfig::loadComplete()
{
    using namespace BC::Store::Digi;
    int n = arrayStoreSize(an);
    for(int i=0; i<n; ++i)
    {
        bool e = retrieveArrayValue(an,i,en,false);
        if(e)
        {
            AnalogChannel c{retrieveArrayValue(an,i,fs,0.0),
                        retrieveArrayValue(an,i,offset,0.0)};
            d_analogChannels.insert_or_assign(i,c);
        }
    }
    n = arrayStoreSize(dig);
    for(int i=0; i<n; ++i)
    {
        bool e = retrieveArrayValue(dig,i,en,false);
        if(e)
        {
            DigitalChannel c{};
            d_digitalChannels.insert_or_assign(i,c);
        }
    }

    d_triggerChannel = retrieve(trigCh,0);
    d_triggerSlope = retrieve(trigSlope,RisingEdge);
    d_triggerLevel = retrieve(trigLevel,0.0);
    d_triggerDelayUSec = retrieve(trigDelay,0.0);
    d_bytesPerPoint = retrieve(bpp,1);
    d_byteOrder = retrieve(bo,DigitizerConfig::LittleEndian);
    d_sampleRate = retrieve(sRate,0.0);
    d_recordLength = retrieve(recLen,0);
    d_blockAverage = retrieve(blockAvg,false);
    d_numAverages = retrieve(numAvg,1);
    d_multiRecord = retrieve(multiRec,false);
    d_numRecords = retrieve(multiRecNum,1);
}
