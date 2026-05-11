#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_chip_info.h>
#include "esp_flash.h"
#include "esp_err.h"
#include "esp_log.h"

#include <IntervalTimer.h>
#include "LedStripDriver.h"
#include <BleConnection.h>

static const char* TAG = "Main   ";
TaskHandle_t xHandle = NULL;

LedStripDriver ledStrip;
BleConnection bleConnection;
static bool m_bleRequest = false;

static void loop(void *pvParameters) {
    IntervalTimer m_timer;

    m_timer.setInterval(2000);

    ledStrip.setup();

    ledStrip.blink(1000);

    // Print chip information
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed\r\n");
        return;
    }

    printf("%uMB %s flash\n", flash_size / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %d bytes\n", esp_get_minimum_free_heap_size());

    while(1) {
        Sliceable::sliceAll( );

        if(m_timer.isNextInterval()) {
            printf("Tick: %lu\r\n", HW_getMillis());
        }

        if(!m_bleRequest && HW_getMillis() > 30000) {
            ESP_LOGI(TAG, "Requesting BLE scan");
            bleConnection.requestScan();
            m_bleRequest = true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main() {

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("Main   ", ESP_LOG_INFO);
    esp_log_level_set("LedStr ", ESP_LOG_INFO);
    esp_log_level_set("BleCon ", ESP_LOG_INFO);

    ESP_LOGI(TAG, "Main Startup");

    uint32_t ret = xTaskCreate(&loop, "loop", 4096, NULL, 5, &xHandle);

    ESP_LOGI(TAG, "Task create returned %lu", ret);

    vTaskDelay(10000 / portTICK_PERIOD_MS);

    // for (int i = 10; i >= 0; i--) {
    //     printf("Restarting in %d seconds...\n", i);
    //     vTaskDelay(2000 / portTICK_PERIOD_MS);
    // }
    // printf("Restarting now.\n");
    // fflush(stdout);
    // esp_restart();
}