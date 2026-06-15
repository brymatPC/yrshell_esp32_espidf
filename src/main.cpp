#include "AppManager.h"
#include "LedStripDriver.h"
#include "BleConnection.h"
#include "WifiConnection.h"
#include "TelnetServer.h"
#include "YRShellEsp32.h"
#include "SdLogger.h"
#include "SystemStatus.h"
#include "UploadDataClient.h"
#include "VictronDevice.h"
#include "Utilities.h"
#include "TempHumidityParser.h"
#include "Sen66Device.h"
#include "AdcDriver.h"

// ESP / C Libraries
#include <stdio.h>
#include <nvs_flash.h>
#include <esp_littlefs.h>
#include <esp_netif_sntp.h>
#include <driver/usb_serial_jtag.h>

// External components
#include <CircularQ.h>
#include <MultiplexerQ.h>

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
CircularQ<char, LOCAL_LOG_BUFFER_SIZE> m_telnetLogQ;
AppManager appMgr(s_appName, s_appVersion);
AdcDriver adc;
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
TempHumidityParser tempHumParser;

Sen66Device sen66Device;

MultiplexerQ serialMux;

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

bool logOut(char c, CircularQBase<char> *queue) {
    static char logOverflow[] = "\r\n\nLOG DATA DROPPED\r\n\n";
    if(queue == nullptr) return false;
    bool ret = true;
    if( queue->spaceAvailable( 24)) {
        queue->put( c);
    } else {
        char *s = logOverflow;
        ret = false;
        queue->reset();
        while( *s != '\0') {
            queue->put( *s++);
        }
    }
    return ret;
}
// TODO: This could be improved, probably shouldn't have a large local buffer on the stack and
//   writing each byte individually could be faster.
int custom_log_handler(const char* format, va_list args) {
    char m_logBuf[256];
    // Format the message into a buffer
    int ret = vsnprintf(m_logBuf, sizeof(m_logBuf), format, args);
    char *s = m_logBuf;
    while( *s != '\0') {
        if(!logOut( *s, &m_logQ)) {
            break;
        }
        // Ignore failure on telnet log Q
        logOut(*s, &m_telnetLogQ);
        s++;
    }
    return ret; 
}

void preSleepNotification(void) {
    bleConnection.off();
    wifiConnection.off();
    sdLogger.stop();
    ledStrip.off();
    if(sen66Device.enabled()) {
        sen66Device.save();
    }
}

bool sleepReady(void) {
   return bleConnection.isOff() && wifiConnection.isOff();
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

void configureUsbSerial() {
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    usb_serial_jtag_driver_install(&usb_serial_jtag_config);
}

static void loop(void *pvParameters) {
    unsigned telnetPort = 23;
    unsigned telnetLogPort = 2023;
    bool wifiConnected = false;

    appMgr.init();
    appMgr.setPreSleepCallback(preSleepNotification);
    appMgr.setSleepReadyCallback(sleepReady);
    ledStrip.setup();
    adc.init();
    bleConnection.setup();
    bleConnection.addParser(BleParserTypes::victron, &victronParser);
    bleConnection.addParser(BleParserTypes::tempHumidity, &tempHumParser);


    wifiConnection.setup();
    wifiConnection.enable();

    if( telnetLogPort != 0) {
        telnetLogServer.init( telnetLogPort);
        telnetLogServer.enable(true);
    }

    if( telnetPort != 0) {
        //telnetServer.init( telnetPort, &shell.getInq(), &shell.getOutq());
    }

    uploadClient.init();
    uploadClient.setup();

    shell.setLedDriver(&ledStrip);
    shell.setAppMgr(&appMgr);
    shell.setWifiConnection(&wifiConnection);
    shell.setBleConnection(&bleConnection);
    shell.setVictronDevice(&victronParser);
    shell.setTempHumParser(&tempHumParser);
    shell.setSen66Device(&sen66Device);
    shell.setLedStrip(&ledStrip);
    shell.setTelnetLogServer(&telnetLogServer);
    shell.setUploadClient(&uploadClient);
    shell.setAdcDriver(&adc);
    shell.init();

    sdLogger.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    systemStatus.setUploadClient(&uploadClient);
    systemStatus.setSdLogger(&sdLogger);
    victronParser.setup();
    victronParser.setUploadClient(&uploadClient);
    victronParser.setSdLogger(&sdLogger);
    tempHumParser.setUploadClient(&uploadClient);
    tempHumParser.setSdLogger(&sdLogger);
    sen66Device.setup();
    sen66Device.setUploadClient(&uploadClient);
    sen66Device.setSdLogger(&sdLogger);

    serialMux.init();
    serialMux.set(0, nullptr, &m_logQ);
    serialMux.set(1, &shell.getInq(), &shell.getOutq());

    startSntp();

    uploadClient.updateWifiStatus(wifiConnected, wifiConnection.getHostIp());

    while(1) {
        Sliceable::sliceAll( );

        if(!wifiConnected && wifiConnection.isNetworkConnected()) {
            wifiConnected = true;
        } else if(wifiConnected && !wifiConnection.isNetworkConnected()) {
            wifiConnected = false;
        }

        //CircularByteQ *outQ = &m_logQ;
        CircularByteQ *outQ = serialMux.getOutQ();
        if( outQ->valueAvailable()) {
            char c;
            for( uint8_t i = 0; i < 32 && outQ->valueAvailable(); i++) {
                c = outQ->get();
            #ifdef YRSHELL_ON_TELNET
                usb_serial_jtag_write_bytes(&c, 1, 0);
            #endif
            }
        }

        bool telnetSpaceAvailable = telnetLogServer.spaceAvailable( 32);
        if( m_telnetLogQ.valueAvailable() && telnetSpaceAvailable) {
            char c;
            for( uint8_t i = 0; i < 32 && m_telnetLogQ.valueAvailable(); i++) {
                c = m_telnetLogQ.get();
                telnetLogServer.put( c);
            }
        }

        uint16_t count = 0;
        while (serialMux.getInQ()->spaceAvailable() && count < 32) {
            uint8_t c;
            int available = usb_serial_jtag_read_bytes(&c, 1, 0);
            if(available > 0) {
                serialMux.getInQ()->put(c);
                count++;
            } else {
                break;
            }
        }
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
    esp_log_level_set("SDCard ", ESP_LOG_INFO);
    esp_log_level_set("MuxQ   ", ESP_LOG_DEBUG);

    // Other libraries
    esp_log_level_set("wifi", ESP_LOG_INFO);
    esp_log_level_set("NimBLE", ESP_LOG_WARN);
    esp_log_level_set("sensirion_i2c_hal", ESP_LOG_WARN);

    ESP_LOGI(TAG, "Main Startup");

    mountLittleFs();

    configureUsbSerial();

    uint32_t ret = xTaskCreatePinnedToCore(&loop, "loop", 4096, NULL, 5, &xHandle, 1);
    ESP_LOGI(TAG, "Task create returned %lu", ret);

    //Create and start stats task
    xTaskCreatePinnedToCore(runCpuPerfTask, "cpuPerf", 4096, NULL, 3, NULL, 0);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
}