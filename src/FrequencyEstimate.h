#ifndef FREQUENCY_ESTIMATE_H_
#define FREQUENCY_ESTIMATE_H_

#include <Sliceable.h>
#include <CircularQ.h>

class FrequencyEstimate : public Sliceable {
private:
    CircularQ<uint16_t, 2048> m_inQ;
    CircularQ<uint16_t, 1024> m_aveQ;
    uint32_t m_runningAverage;
public:
    FrequencyEstimate();
    virtual ~FrequencyEstimate();
    virtual const char* sliceName() { return "FreqEst"; }
    void slice();
    void setInQ(CircularQBase<uint16_t> *prevQ);
};

#endif // FREQUENCY_ESTIMATE_H_