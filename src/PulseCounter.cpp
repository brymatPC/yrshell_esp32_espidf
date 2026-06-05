#include "PulseCounter.h"
#include "esp_log_custom.h"
#include "Utilities.h"


static const char* TAG = "Pulse  ";

PulseCounter::PulseCounter(uint8_t pin) :
    m_pin(pin),
    m_handle(NULL),
    m_chanHandle(NULL),
    m_startTimeUs(0),
    m_initialized(false)
{
    m_timer.setInterval(1000);
}
PulseCounter::~PulseCounter() {}
void PulseCounter::init() {
    esp_err_t err;
    pcnt_unit_config_t config = {};
    config.high_limit = 1000;
    config.low_limit = -1000;

    err = pcnt_new_unit(&config, &m_handle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register new pcnt unit, error %lu", err);
        return;
    }

    // pcnt_glitch_filter_config_t filter_config = {
    //     .max_glitch_ns = 1000,
    // };
    // ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(m_handle, &filter_config));

    pcnt_chan_config_t chanConfig = {
        .edge_gpio_num = (int) m_pin,
        .level_gpio_num = -1,
        .flags = {
            .invert_edge_input = false,
            .invert_level_input = false,
            .virt_edge_io_level = 0,
            .virt_level_io_level = 0
        }
    };
    err = pcnt_new_channel(m_handle, &chanConfig, &m_chanHandle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register new pcnt channel, error %lu", err);
        return;
    }

    err = pcnt_channel_set_edge_action(m_chanHandle, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set edge action, error %lu", err);
        return;
    }

    err = pcnt_unit_enable(m_handle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable, error %lu", err);
        return;
    }
    err = pcnt_unit_clear_count(m_handle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear count, error %lu", err);
        return;
    }
    err = pcnt_unit_start(m_handle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start, error %lu", err);
        return;
    }
    m_startTimeUs = HW_getMicros();
    m_initialized = true;
}
void PulseCounter::slice() {
    if(!m_initialized) return;
    if(m_timer.isNextInterval()) {
        uint32_t duration = HW_getMicros() - m_startTimeUs;
        int pulseCount = 0;
        esp_err_t err;
        err = pcnt_unit_get_count(m_handle, &pulseCount);
        if(err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get pulse count, error %lu", err);
            return;
        }
        err = pcnt_unit_clear_count(m_handle);
        if(err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear count, error %lu", err);
            return;
        }
        m_startTimeUs = HW_getMicros();
        float freq = calculateFrequency(duration, pulseCount);
        ESP_LOGI(TAG, "Est freq %.2f, duration %lu, count %d", freq, duration, pulseCount);
    }
}

float PulseCounter::calculateFrequency(uint32_t durationUs, int pulseCount) {
    if(durationUs == 0 || pulseCount <= 0) return 0.0f;

    float durSec = ((float) durationUs) / 1000000.0f;
    float count = (float) pulseCount;

    return count / durSec;
}