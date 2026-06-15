#ifndef ADC_DRIVER_H_
#define ADC_DRIVER_H_

#include <Sliceable.h>
#include <esp_adc/adc_continuous.h>

#define ADC_MAX_BUFFER_SIZE (1024)
#define ADC_READ_LEN        (128)
#define ADC_DEFAULT_SAMPLE_FREQ_HZ (1000)


class AdcDriver : public Sliceable  {
private:
    adc_continuous_handle_t m_adcHandle;
    uint32_t m_sampleFrequency;
    bool m_initialized;

public:
    AdcDriver();
    virtual ~AdcDriver();
    virtual const char* sliceName( void) { return "AdcDriver"; }
    void init();
    virtual void slice( void);

    void logIoNumbers();
    void getAdcLocation(int gpio, uint8_t *adcUnit, uint8_t *adcChan);
};

#endif // ADC_DRIVER_H_