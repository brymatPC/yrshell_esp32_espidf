#include "AdcDriver.h"
#include "Utilities.h"
#include "esp_log_custom.h"

#include <esp_adc/adc_continuous.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

static const char* TAG = "AdcDrv ";

// NOTE: ISR Context
static bool poolOverflow(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) {
    AdcDriver *adcDriver = (AdcDriver *)user_data;
    adcDriver->poolOverflowISR();
    return false;
}

AdcDriver::AdcDriver() :
    m_adcHandle(NULL),
    m_sampleFrequency(ADC_DEFAULT_SAMPLE_FREQ_HZ),
    m_initialized(false),
    m_numSamplesRead(0),
    m_curChannel(0),
    m_numPoolOverflows(0)
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

    adc_continuous_evt_cbs_t callbacks = {
        .on_conv_done = NULL,
        .on_pool_ovf = poolOverflow,
    };
    err = adc_continuous_register_event_callbacks(m_adcHandle, &callbacks, this);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event callbacks, err: %lu", err);
    }

    ESP_LOGI(TAG, "Adc initialized");
    m_initialized = true;
}
void AdcDriver::slice() {
    static bool errorLogged = false;
    if(m_running) {
        uint32_t numSamples = 0;
        esp_err_t ret = adc_continuous_read_parse(m_adcHandle, m_adcBuf, ADC_READ_LEN, &numSamples, 0);
        if (ret == ESP_OK) {
            m_numSamplesRead += numSamples;

            for(uint16_t i=0; i < numSamples; i++) {
                if(m_adcBuf[i].valid) {
                    if(!m_chan0Buf.spaceAvailable()) {
                        m_chan0Average -= m_chan0Buf.get();
                    }
                    m_chan0Buf.put((uint16_t) m_adcBuf[i].raw_data);
                    m_chan0Average += m_adcBuf[i].raw_data;
                }
            }

        } else if (ret == ESP_ERR_TIMEOUT) {
            // Data not available yet, just ignore
        } else {
            if(!errorLogged) {
                ESP_LOGE(TAG, "Failed to read and parse adc data, err: %d", ret);
                errorLogged = true;
            }
        }

        if(m_logTimer.isNextInterval()) {
            uint32_t average = m_chan0Average / m_chan0Buf.used();
            int voltage = 0;
            esp_err_t err = adc_cali_raw_to_voltage(m_adcCalibHandles[0], average, &voltage);
            if(err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to convert raw voltage on index %u, err: %d", 0, err);
            }

            ESP_LOGI(TAG, "Chan 0 average is %lu counts, %d mV, from %lu values", average, voltage, m_chan0Buf.used());
        }
    } else {
        errorLogged = false;
    }
}
int AdcDriver::addChannel(int gpio, uint8_t attenuation) {
    if(!m_initialized) {
        ESP_LOGE(TAG, "ADC not initialized, can't add channel");
        return -1;
    }
    if(m_running) {
        ESP_LOGE(TAG, "ADC running, can't add channel");
        return -1;
    }
    if(m_curChannel >= SOC_ADC_PATT_LEN_MAX) {
        ESP_LOGE(TAG, "All channels configured, can't add channel");
        return -1;
    }
    if(attenuation > ADC_ATTEN_DB_12) {
        ESP_LOGE(TAG, "Invalid attenuation: %u, can't add channel", attenuation);
        return -1;
    }

    adc_unit_t unit;
    adc_channel_t chan;
    esp_err_t err = adc_continuous_io_to_channel(gpio, &unit, &chan);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get adc channel from io %d, err: %lu", gpio, err);
        return -1;
    }

    m_channelConfig[m_curChannel] = {};
    m_channelConfig[m_curChannel].atten = (adc_atten_t) attenuation;
    m_channelConfig[m_curChannel].unit = unit;
    m_channelConfig[m_curChannel].channel = chan;
    m_channelConfig[m_curChannel].bit_width = ADC_BITWIDTH_12;
    createCalibHandle(m_curChannel);
    ESP_LOGI(TAG, "Adc chan %u configured to unit %u, adc channel %u", m_curChannel, unit, chan);
    m_curChannel++;
    return ESP_OK;
}
void AdcDriver::clearChannels() {
    if(m_running) {
        ESP_LOGE(TAG, "ADC running, can't clear channels");
        return;
    }
    m_curChannel = 0;
}
void AdcDriver::setFrequency(uint32_t freqHz) {
    if(m_running) {
        ESP_LOGE(TAG, "ADC running, can't set frequency");
    }
    if(freqHz < SOC_ADC_SAMPLE_FREQ_THRES_LOW || freqHz > SOC_ADC_SAMPLE_FREQ_THRES_HIGH) {
        ESP_LOGE(TAG, "Frequency %lu is out of range, must be between %lu and %lu", freqHz, SOC_ADC_SAMPLE_FREQ_THRES_LOW, SOC_ADC_SAMPLE_FREQ_THRES_HIGH);
        return;
    }
    m_sampleFrequency = freqHz;
    ESP_LOGI(TAG, "ADC frequency set to %lu Hz", m_sampleFrequency);
}
void AdcDriver::start() {
    esp_err_t err;
    if(!m_initialized) return;

    adc_continuous_config_t adcChanConfig = {};
    adcChanConfig.sample_freq_hz = m_sampleFrequency;
    // TODO: This needs to change if both units are configured
    adcChanConfig.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    adcChanConfig.pattern_num = m_curChannel;
    adcChanConfig.adc_pattern = m_channelConfig;

    err = adc_continuous_config(m_adcHandle, &adcChanConfig);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure adc channels, err: %lu", err);
        return;
    }
    m_numPoolOverflows = 0;
    err = adc_continuous_start(m_adcHandle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start adc, err: %lu", err);
    } else {
        m_numSamplesRead = 0;
        m_startTime = HW_getMillis();
        m_logTimer.setInterval(2000);
        m_chan0Average = 0;
        m_chan0Buf.reset();
        m_running = true;
        ESP_LOGI(TAG, "Adc started");
    }
}
void AdcDriver::stop() {
    if(!m_initialized || !m_running) return;
    esp_err_t err = adc_continuous_stop(m_adcHandle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop adc, err: %lu", err);
    } else {
        ESP_LOGI(TAG, "Adc stopped");
        m_running = false;
        ESP_LOGI(TAG, "Read %lu samples in %lu ms; Num pool overflows %lu", m_numSamplesRead, HW_getMillis() - m_startTime, m_numPoolOverflows);
    }
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
void AdcDriver::getAdcVref(uint32_t *vrefMv) {
    esp_err_t err;

    adc_cali_curve_fitting_config_t config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_12
    };
    adc_cali_handle_t caliHandle;
    err = adc_cali_create_scheme_curve_fitting(&config, &caliHandle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create adc calibration handle, err: %lu", err);
        return;
    }

    int voltage = 0;
    err = adc_cali_raw_to_voltage(caliHandle, 4095, &voltage);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert adc raw value, err: %lu", err);
        *vrefMv = 0;
    } else {
        *vrefMv = (uint32_t) voltage;
    }

    err = adc_cali_delete_scheme_curve_fitting(caliHandle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete adc calibration handle, err: %lu", err);
    }
}
void AdcDriver::createCalibHandle(uint8_t index) {
    if(index >= SOC_ADC_PATT_LEN_MAX) return;
    esp_err_t err;
    adc_cali_curve_fitting_config_t config = {
        .unit_id = (adc_unit_t) m_channelConfig[index].unit,
        .chan = (adc_channel_t) m_channelConfig[index].channel,
        .atten = (adc_atten_t) m_channelConfig[index].atten,
        .bitwidth = ADC_BITWIDTH_12
    };
    err = adc_cali_create_scheme_curve_fitting(&config, &m_adcCalibHandles[index]);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create adc calibration handle for index %u, err: %lu", index, err);
        return;
    }
}

void AdcDriver::poolOverflowISR() {
    m_numPoolOverflows++;
}