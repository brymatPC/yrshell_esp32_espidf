#include "PulseCapture.h"
#include "esp_log_custom.h"
#include "Utilities.h"

// ESP / C Libraries
#include <esp_clk_tree.h>

static const char* TAG = "PulseC ";

#define MAX_NUM_CAPTURES 32
static uint32_t m_captures[2][MAX_NUM_CAPTURES];
static uint32_t m_captureIndex[2];
static uint32_t m_numCaptures[2];

static bool captureCallback(mcpwm_cap_channel_handle_t cap_chan, const mcpwm_capture_event_data_t *edata, void *user_data) {
    static uint32_t lastCapture = 0;
    if(edata->cap_edge == MCPWM_CAP_EDGE_POS) {
        uint8_t group = *((uint8_t *) user_data);
        m_captures[group][m_captureIndex[group]] = edata->cap_value - lastCapture;
        lastCapture = edata->cap_value;
        m_captureIndex[group]++;
        if(m_captureIndex[group] >= MAX_NUM_CAPTURES) {
            m_captureIndex[group] = 0;
        }
        m_numCaptures[group]++;
    }
    return true;
}

PulseCapture::PulseCapture(uint8_t pin, uint8_t group) :
    m_pin(pin),
    m_group(group),
    m_handle(NULL),
    m_apbFreq(1),
    m_initialized(false)
{
    m_timer.setInterval(5000);

    for(uint32_t i=0; i < MAX_NUM_CAPTURES; i++) {
        m_captures[0][i] = 0;
        m_captures[1][i] = 0;
    }
    m_captureIndex[0] = 0;
    m_captureIndex[1] = 0;
    m_numCaptures[0] = 0;
    m_numCaptures[1] = 0;
}
PulseCapture::~PulseCapture() {}
void PulseCapture::init() {
    esp_err_t err;
    mcpwm_capture_timer_config_t config = {};
    config.group_id = (int) m_group;
    config.clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT;

    err = mcpwm_new_capture_timer(&config, &m_handle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register new capture unit, error %lu", err);
        return;
    }

    mcpwm_capture_channel_config_t cap_ch_conf = {
        .gpio_num = (int) m_pin,
        .intr_priority = 0,
        .prescale = 1,
        .flags = {
            .pos_edge = true,
            .neg_edge = false,
            .invert_cap_signal = false,
        }
    };
    err = mcpwm_new_capture_channel(m_handle, &cap_ch_conf, &m_chanHandle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register new capture channel, error %lu", err);
        return;
    }

    mcpwm_capture_event_callbacks_t cbs = {
        .on_cap = captureCallback,
    };
    err = mcpwm_capture_channel_register_event_callbacks(m_chanHandle, &cbs, &m_group);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register callback, error %lu", err);
        return;
    }

    err = mcpwm_capture_channel_enable(m_chanHandle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable channel, error %lu", err);
        return;
    }
    err = mcpwm_capture_timer_enable(m_handle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable capture, error %lu", err);
        return;
    }
    err = mcpwm_capture_timer_start(m_handle);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start, error %lu", err);
        return;
    }
    
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_APB, ESP_CLK_TREE_SRC_FREQ_PRECISION_APPROX, &m_apbFreq);
    m_initialized = true;
}
void PulseCapture::slice() {
    if(!m_initialized) return;
    if(m_timer.isNextInterval()) {
        float freq = calculateFrequency();
        uint32_t numCaptures = m_numCaptures[m_group];
        m_numCaptures[m_group] = 0;
        ESP_LOGI(TAG, "%d - Est freq %.2f, numCaptures %d", m_group, freq, numCaptures);
        
    }
}

float PulseCapture::calculateFrequency() {
    uint32_t averageInterval = 0;
    for(uint32_t i=0; i < MAX_NUM_CAPTURES; i++) {
        averageInterval += m_captures[m_group][i];
    }
    float average = ((float) averageInterval) / ((float) MAX_NUM_CAPTURES);
    float clkFreq = (float) m_apbFreq;
    return clkFreq / average;
}