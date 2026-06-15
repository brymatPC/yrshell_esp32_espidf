#include "AdcDriver.h"
#include "esp_log_custom.h"

#include <esp_adc/adc_continuous.h>

static const char* TAG = "AdcDrv ";

AdcDriver::AdcDriver() :
    m_adcHandle(NULL),
    m_sampleFrequency(ADC_DEFAULT_SAMPLE_FREQ_HZ),
    m_initialized(false)
{

}
AdcDriver::~AdcDriver() {
}
void AdcDriver::init() {
    esp_err_t err;

    adc_continuous_handle_cfg_t adcConfig = {
        .max_store_buf_size = ADC_MAX_BUFFER_SIZE,
        .conv_frame_size = ADC_READ_LEN,
        .flags = {
            .flush_pool = false
        }
    };

    err = adc_continuous_new_handle(&adcConfig, &m_adcHandle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new adc handle, err: %lu", err);
        m_adcHandle = NULL;
        return;
    }

    ESP_LOGI(TAG, "Adc initialized");
    m_initialized = true;

    // adc_continuous_config_t adcChannelConfig = {};
    // adcChannelConfig.sample_freq_hz = m_sampleFrequency;
    // adcChannelConfig.conv_mode = ADC_CONV_SINGLE_UNIT_1;

    // ADC_CHANNEL_6;
    
    // adc_continuous_io_to_channel();

}
void AdcDriver::slice() {

}
void AdcDriver::logIoNumbers() {
    for(uint8_t i=0; i < SOC_ADC_PERIPH_NUM; i++) {
        for(uint8_t j=0; j < SOC_ADC_MAX_CHANNEL_NUM; j++) {
            int ioNum;
            adc_continuous_channel_to_io((adc_unit_t) i, (adc_channel_t) j, &ioNum);
            ESP_LOGI(TAG, "Adc unit %u, chan %u is gpio %d", i, j, ioNum);
        }
    }
}
void AdcDriver::getAdcLocation(int gpio, uint8_t *adcUnit, uint8_t *adcChan) {
    adc_unit_t unit;
    adc_channel_t chan;
    esp_err_t err = adc_continuous_io_to_channel(gpio, &unit, &chan);
    if(err == ESP_OK) {
        *adcUnit = (uint8_t) unit;
        *adcChan = (uint8_t) chan;
    } else {
        ESP_LOGW(TAG, "Failed to get adc channel from io %d, err: %lu", gpio, err);
        *adcUnit = 0;
        *adcChan = 0;
    }
}