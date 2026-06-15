#ifndef ADC_DRIVER_H_
#define ADC_DRIVER_H_

#include <Sliceable.h>
#include <CircularQ.h>
#include <IntervalTimer.h>
#include <esp_adc/adc_continuous.h>
#include <esp_adc/adc_cali.h>

#define ADC_MAX_BUFFER_SIZE (1024)
#define ADC_READ_LEN        (128)
#define ADC_DEFAULT_SAMPLE_FREQ_HZ (SOC_ADC_SAMPLE_FREQ_THRES_LOW)
#define ADC_MAX_CHANNELS    (8)


class AdcDriver : public Sliceable  {
private:
    adc_continuous_handle_t m_adcHandle;
    uint32_t m_sampleFrequency;
    bool m_initialized;
    bool m_running;
    uint32_t m_numSamplesRead;
    uint32_t m_startTime;

    adc_digi_pattern_config_t m_channelConfig[SOC_ADC_PATT_LEN_MAX];
    adc_cali_handle_t m_adcCalibHandles[SOC_ADC_PATT_LEN_MAX];
    uint8_t m_curChannel;

    adc_continuous_data_t m_adcBuf[ADC_READ_LEN];

    CircularQ<uint16_t, 512> m_chan0Buf;
    uint32_t m_chan0Average;

    uint32_t m_numPoolOverflows;

    IntervalTimer m_logTimer;
public:
    AdcDriver();
    virtual ~AdcDriver();
    virtual const char* sliceName( void) { return "AdcDriver"; }
    void init();
    virtual void slice( void);

    int addChannel(int gpio, uint8_t attenuation);
    void clearChannels();
    void setFrequency(uint32_t freqHz);
    void start();
    void stop();

    void logIoNumbers();
    void getAdcLocation(int gpio, uint8_t *adcUnit, uint8_t *adcChan);
    void getAdcVref(uint32_t *vrefMv);
    void createCalibHandle(uint8_t index);

    // ISR Context
    void poolOverflowISR();
};

#endif // ADC_DRIVER_H_