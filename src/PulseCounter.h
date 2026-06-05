#ifndef PULSE_COUNTER_H_
#define PULSE_COUNTER_H_

#include <driver/pulse_cnt.h>

#include <IntervalTimer.h>
#include <Sliceable.h>

class PulseCounter : public Sliceable {
private:
    uint8_t m_pin;
    pcnt_unit_handle_t m_handle;
    pcnt_channel_handle_t m_chanHandle;
    uint32_t m_startTimeUs;
    bool m_initialized;

    IntervalTimer m_timer;

    float calculateFrequency(uint32_t durationUs, int pulseCount);
public:
    PulseCounter(uint8_t pin);
    virtual ~PulseCounter();
    virtual const char* sliceName( void) { return "PulseCounter"; }
    void init();
    virtual void slice( void);
};

#endif //PULSE_COUNTER_H_