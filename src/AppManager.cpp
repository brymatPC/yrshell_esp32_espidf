#include "AppManager.h"
#include "Utilities.h"
#include "esp_log_custom.h"

// ESP / C Libraries
#include <esp_sleep.h>
#include <nvs.h>
#include <esp_app_desc.h>

// External components
#include <HardwareSpecific.h>

static char s_resetUnknownStr[]     = "unknown";
static char s_resetPowerOnStr[]     = "power-on";
static char s_resetExtStr[]         = "external";
static char s_resetSoftwareStr[]    = "Software";
static char s_resetPanicStr[]       = "panic";
static char s_resetIntWdogStr[]     = "interrupt watchdog";
static char s_resetTaskWdogStr[]    = "task watchdog";
static char s_resetOthWdogStr[]     = "other watchdog";
static char s_resetDeepSleepStr[]   = "deep sleep";
static char s_resetBrownOutStr[]    = "brown out";
static char s_resetSDIOStr[]        = "sdio";
static char s_resetUsbStr[]         = "usb";
static char s_resetJtagStr[]        = "jtag";
static char s_resetEFuseStr[]       = "efuse";
static char s_resetPowerGlitchStr[] = "power glitch";
static char s_resetCpuLockupStr[]   = "cpu lockup";

static const char* TAG = "AppMgr ";

const char AppManager::s_PREF_NAMESPACE[] = "sys";
const uint32_t AppManager::s_DEFAULT_RUN_TIME_MS = 0;
const uint32_t AppManager::s_DEFAULT_SLEEP_TIME_MS = 0;
const uint32_t AppManager::s_STATUS_INTERVAL_MS = 30000;

RTC_DATA_ATTR static int m_bootCount = 0;

typedef enum {
  STATE_RESET      = 0,
  STATE_RUNNING    = 1,
  STATE_SLEEP_REQ  = 2,
  STATE_SLEEP      = 3,
  STATE_SLEEP_WAIT = 5,
  STATE_OFF        = 4,

} appStates_t;

AppManager::AppManager(const char* appName, const char* appVersion) :
    m_appName(appName),
    m_appVersion(appVersion),
    m_runTimeMs(s_DEFAULT_RUN_TIME_MS),
    m_sleepTimeMs(s_DEFAULT_SLEEP_TIME_MS),
    m_sleepEnabled(true),
    m_state(STATE_RESET)
{
    m_bootCount++;

    m_timer.setInterval(5000);
}
AppManager::~AppManager() {

}

void AppManager::init() {
    esp_err_t err;
    uint32_t _handle;
    
    // BAM - 20260602 - Need to set this here rather than constructor as it isn't available yet in the constructor
    resetReasonStartup = esp_reset_reason();

    err = nvs_open(s_PREF_NAMESPACE, NVS_READONLY, &_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs partition, err: %lu", err);
        return;
    }
    m_runTimeMs = s_DEFAULT_RUN_TIME_MS;
    err = nvs_get_u32(_handle, "runt", &m_runTimeMs);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_u32 fail: runt - %lu", err);
    }
    m_sleepTimeMs = s_DEFAULT_SLEEP_TIME_MS;
    err = nvs_get_u32(_handle, "slpt", &m_sleepTimeMs);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_u32 fail: slpt - %lu", err);
    }
    nvs_close(_handle);
}
void AppManager::save() {
    esp_err_t err;
    uint32_t _handle;
    err = nvs_open(s_PREF_NAMESPACE, NVS_READWRITE, &_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs partition, err: %lu", err);
        return;
    }
    err = nvs_set_u32(_handle, "runt", m_runTimeMs);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u32 fail: runt -  %lu", err);
    }
    err = nvs_set_u32(_handle, "slpt", m_sleepTimeMs);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u32 fail: slpt -  %lu", err);
    }
    err = nvs_commit(_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit fail: %lu", err);
    }
    nvs_close(_handle);
    ESP_LOGI(TAG, "Preferences updated");
}
void AppManager::printAppInfo(void) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Project Version/Git Hash: %s", app_desc->version);
    ESP_LOGI(TAG, "Project Compile Date/Time: %s %s", app_desc->date, app_desc->time);
    ESP_LOGI(TAG, "IDF Version: %s", app_desc->idf_ver);
}

void AppManager::slice( void) {
    if(m_timer.hasIntervalElapsed()) {
        m_timer.setInterval(s_STATUS_INTERVAL_MS);
        const char *resetStr = resetReasonToString(resetReasonStartup);
        ESP_LOGI(TAG, "System Status Only, not a reboot!");
        ESP_LOGI(TAG, "AppName: %s; AppVersion: %s", m_appName, m_appVersion);
        ESP_LOGI(TAG, "bootCount: %d", m_bootCount);
        ESP_LOGI(TAG, "Reset Reason: %d, %s", resetReasonStartup, resetStr);
        printFrequencies();
        printAppInfo();
    }

    switch(m_state) {
        case STATE_RESET:
            m_state = STATE_RUNNING;
        break;
        case STATE_RUNNING:
            if(m_sleepEnabled && m_runTimeMs > 0 && m_sleepTimeMs > 0 && HW_getMillis() > m_runTimeMs) {
                ESP_LOGI(TAG, "Sleep enabled - sleepTimeMs: %lu", m_sleepTimeMs);
                if(preSleep_cb) {
                    preSleep_cb();
                }
                m_state = STATE_SLEEP_REQ;
            }
        break;
        case STATE_SLEEP_REQ:
            // Force sleep after 3 seconds
            if(HW_getMillis() > m_runTimeMs + 3000) {
                ESP_LOGW(TAG, "Sleep ready took too long, going to sleep anyway");
                m_sleepWaitTimer = HW_getMillis();
                m_state = STATE_SLEEP_WAIT;
            } else if(sleepReady_cb == nullptr || sleepReady_cb()) {
                m_sleepWaitTimer = HW_getMillis();
                m_state = STATE_SLEEP_WAIT;
            }
        break;
        case STATE_SLEEP_WAIT:
            if(HW_getMillis() > m_sleepWaitTimer + 5000) {
                m_state = STATE_SLEEP;
            }
        break;
        case STATE_SLEEP:
            // No exit from this state
            esp_deep_sleep(1000LL * m_sleepTimeMs);
        break;
        default:
            m_state = STATE_RESET;
        break;
    }
}

const char *AppManager::resetReasonToString(esp_reset_reason_t reason) {
    char *result;
    switch (reason) {
    case ESP_RST_POWERON:
        result = s_resetPowerOnStr;
        break;
    case ESP_RST_EXT:
        result = s_resetExtStr;
        break;
    case ESP_RST_SW:
        result = s_resetSoftwareStr;
        break;
    case ESP_RST_PANIC:
        result = s_resetPanicStr;
        break;
    case ESP_RST_INT_WDT:
        result = s_resetIntWdogStr;
        break;
    case ESP_RST_TASK_WDT:
        result = s_resetTaskWdogStr;
        break;
    case ESP_RST_WDT:
        result = s_resetOthWdogStr;
        break;
    case ESP_RST_DEEPSLEEP:
        result = s_resetDeepSleepStr;
        break;
    case ESP_RST_BROWNOUT:
        result = s_resetBrownOutStr;
        break;
    case ESP_RST_SDIO:
        result = s_resetSDIOStr;
        break;
    case ESP_RST_USB:
        result = s_resetUsbStr;
        break;
    case ESP_RST_JTAG:
        result = s_resetJtagStr;
        break;
    case ESP_RST_EFUSE:
        result = s_resetEFuseStr;
        break;
    case ESP_RST_PWR_GLITCH:
        result = s_resetPowerGlitchStr;
        break;
    case ESP_RST_CPU_LOCKUP:
        result = s_resetCpuLockupStr;
        break;
    case ESP_RST_UNKNOWN:
    default:
        result = s_resetUnknownStr;
        break;
    }
    return result;
}