#ifndef PULSE_CAPTURE_H_
#define PULSE_CAPTURE_H_

#include <driver/mcpwm_cap.h>

#include <IntervalTimer.h>
#include <Sliceable.h>

#define MAX_NUM_CAPTURES 32

class PulseCapture : public Sliceable {
private:
    uint8_t m_pin;
    uint8_t m_group;
    mcpwm_cap_timer_handle_t m_handle;
    mcpwm_cap_channel_handle_t m_chanHandle;
    uint32_t m_apbFreq;
    bool m_initialized;
    IntervalTimer m_timer;

    uint32_t m_lastCapture;
    uint32_t m_captures[MAX_NUM_CAPTURES];
    uint32_t m_captureIndex;
    uint32_t m_numCaptures;

    float calculateFrequency();
public:
    PulseCapture(uint8_t pin, uint8_t group = 0);
    virtual ~PulseCapture();
    virtual const char* sliceName( void) { return "PulseCapture"; }
    void init();
    virtual void slice( void);

    // ISR Context
    void captureCallbackIsr(uint32_t capValue);
};

#endif //PULSE_CAPTURE_H_