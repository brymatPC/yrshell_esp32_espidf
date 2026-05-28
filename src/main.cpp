#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_chip_info.h>
#include "esp_flash.h"
#include "esp_err.h"
#include "esp_log_custom.h"
#include <nvs_flash.h>

#include <CircularQ.h>
// #include <IntervalTimer.h>
#include "AppManager.h"
#include "LedStripDriver.h"
#include <BleConnection.h>
#include "WifiConnection.h"
#include "TelnetServer.h"
#include "YRShellEsp32.h"
#include "SdLogger.h"
#include "SystemStatus.h"
#include "UploadDataClient.h"
#include "VictronDevice.h"

#include "esp_littlefs.h"
#include "esp_netif_sntp.h"

#define YRSHELL_ON_TELNET
#define LOCAL_LOG_BUFFER_SIZE 8192

static char s_appName[] = "ESP32 BLE Test";
static char s_appVersion[] = "0.9.0";
static const char* TAG = "Main   ";
TaskHandle_t xHandle = NULL;

static const int8_t SD_SCK = 10;
static const int8_t SD_MISO = 7;
static const int8_t SD_MOSI = 8;
static const int8_t SD_CS = 11;

CircularQ<char, LOCAL_LOG_BUFFER_SIZE> m_logQ;
AppManager appMgr(s_appName, s_appVersion);
YRShellEsp32 shell;
LedStripDriver ledStrip;
BleConnection bleConnection;
SdLogger sdLogger;
WifiConnection wifiConnection(&ledStrip, 7500);
TelnetServer telnetServer;
TelnetLogServer telnetLogServer;
UploadDataClient uploadClient;

SystemStatus systemStatus;
VictronDevice victronParser;

void timeSyncNotification(struct timeval *tv) {
    ESP_LOGI(TAG, "Time synchronization event");
}

void startSntp(void) {
    esp_err_t err;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = timeSyncNotification;
    err = esp_netif_sntp_init(&config);
    if(err != ESP_OK) {
      ESP_LOGW(TAG, "SNTP error: %u", err);
    } else {
      ESP_LOGI(TAG, "NTP request started");
    }
}

bool logOut(char c) {
  static char logOverflow[] = "\r\n\nLOG DATA DROPPED\r\n\n";
  bool ret = true;
    if( m_logQ.spaceAvailable( 24)) {
      m_logQ.put( c);
    } else {
      char *s = logOverflow;
      ret = false;
      m_logQ.reset();
      while( *s != '\0') {
        m_logQ.put( *s++);
      }
    }
    return ret;
}
int custom_log_handler(const char* format, va_list args) {
    // Format the message into a buffer
    char buf[128];
    int ret = vsnprintf(buf, sizeof(buf), format, args);
    char *s = buf;
    while( *s != '\0') {
      if(!logOut( *s++)) {
        break;
      }
    }
    return ret; 
}

bool mountLittleFs() {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .partition = NULL,
        .blockdev = NULL,
        .format_if_mount_failed = false,
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = true,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err == ESP_FAIL) {
        ESP_LOGE(TAG, "Mounting LittleFS failed! Error: %d", err);
        return false;
    }
    return true;
}

static void loop(void *pvParameters) {
    unsigned telnetPort = 23;
    unsigned telnetLogPort = 2023;
    bool wifiConnected = false;

    appMgr.init();
    ledStrip.setup();
    bleConnection.setup();
    bleConnection.addParser(BleParserTypes::victron, &victronParser);

    wifiConnection.setup();
    wifiConnection.enable();

    if( telnetLogPort != 0) {
        telnetLogServer.init( telnetLogPort);
        telnetLogServer.enable(true);
    }

    if( telnetPort != 0) {
        telnetServer.init( telnetPort, &shell.getInq(), &shell.getOutq());
    }

    uploadClient.init();
    uploadClient.setup();

    shell.setLedDriver(&ledStrip);
    shell.setAppMgr(&appMgr);
    shell.setWifiConnection(&wifiConnection);
    shell.setBleConnection(&bleConnection);
    shell.setVictronDevice(&victronParser);
    shell.setLedStrip(&ledStrip);
    shell.setTelnetLogServer(&telnetLogServer);
    shell.setUploadClient(&uploadClient);
    shell.init();

    sdLogger.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    systemStatus.setUploadClient(&uploadClient);
    systemStatus.setSdLogger(&sdLogger);
    victronParser.setup();
    victronParser.setUploadClient(&uploadClient);
    victronParser.setSdLogger(&sdLogger);

    startSntp();

    uploadClient.updateWifiStatus(wifiConnected, wifiConnection.getHostIp());

    while(1) {
        Sliceable::sliceAll( );

        if(!wifiConnected && wifiConnection.isNetworkConnected()) {
            wifiConnected = true;
        } else if(wifiConnected && !wifiConnection.isNetworkConnected()) {
            wifiConnected = false;
        }

        bool telnetSpaceAvailable = telnetLogServer.spaceAvailable( 32);
        bool serialSpaceAvailable = true;
        if( m_logQ.valueAvailable() && (telnetSpaceAvailable || serialSpaceAvailable)) {
            char c;
            for( uint8_t i = 0; i < 32 && m_logQ.valueAvailable(); i++) {
                c = m_logQ.get();
                if(telnetSpaceAvailable) {
                    telnetLogServer.put( c);
                }
            #ifdef YRSHELL_ON_TELNET
                if(serialSpaceAvailable) {
                    printf("%c", c);
                }
            #endif
            }
        }

        vTaskDelay(1);
    }
}

extern "C" void app_main() {

    ESP_ERROR_CHECK(nvs_flash_init());

    esp_log_set_vprintf(custom_log_handler);

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("Main   ", ESP_LOG_INFO);
    esp_log_level_set("AppMgr ", ESP_LOG_INFO);
    esp_log_level_set("LedStr ", ESP_LOG_INFO);
    esp_log_level_set("BleCon ", ESP_LOG_INFO);
    esp_log_level_set("WifiCon", ESP_LOG_INFO);
    esp_log_level_set("TelnetS", ESP_LOG_INFO);
    esp_log_level_set("YRShell", ESP_LOG_INFO);
    esp_log_level_set("Perf   ", ESP_LOG_INFO);

    ESP_LOGI(TAG, "Main Startup");

    mountLittleFs();

    uint32_t ret = xTaskCreate(&loop, "loop", 4096, NULL, 5, &xHandle);

    ESP_LOGI(TAG, "Task create returned %lu", ret);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
}