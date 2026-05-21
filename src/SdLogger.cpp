#include "SdLogger.h"

// #include <SPI.h>
// #include <SD.h>
#include <cstring>
#include <cstdlib>

#include "esp_log_custom.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"

static const char* TAG = "SDCard ";

#define MOUNT_POINT "/sdcard"

// For SD Card access
//SPIClass sd_spi(HSPI);

SdLogger::SdLogger() :
    m_cs(0)
{
    m_timer.setInterval(SD_CONN_CHECK_MS);
}

void SdLogger::begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t cs) {
     esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ESP_LOGI(TAG, "Using SPI peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.unaligned_multi_block_rw_max_chunk_size = 8;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize((spi_host_device_t) host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t) cs;
    slot_config.host_id = (spi_host_device_t) host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");
    //sdmmc_card_print_info(stdout, card);
    logSdCardStatus(card);
}

void SdLogger::loop() {
    if(m_timer.isNextInterval()) {
        // ESP_LOGI(TAG, "SD Connection check");
        // if(SD.cardType() == CARD_NONE) {
        //     ESP_LOGI(TAG, "SD card not connected, retrying...");
        //     if(!SD.begin(m_cs, sd_spi)) {
        //         ESP_LOGW(TAG, "SD card not found");
        //     } else {
        //         logSdCardStatus();
        //     }
        // }
    }
}

void SdLogger::logSdCardStatus(sdmmc_card_t *card) {

    esp_err_t err = sdmmc_get_status(card);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get sdmmc status, err %lu", err);
    } else {
        ESP_LOGI(TAG, "SD Card is good");
    }

    if(card->is_mmc){
        ESP_LOGI(TAG, "SD Card Type: MMC");
    } else if(card->is_sdio){
        ESP_LOGI(TAG, "SD Card Type: SDIO");
    } else if(card->is_mem){
        if(card->ocr & (1 << 30)) {
            ESP_LOGI(TAG, "SD Card Type: SDHC");
        } else {
            ESP_LOGI(TAG, "SD Card Type: SD");
        }
    } else {
        ESP_LOGI(TAG, "SD Card Type: UNKNOWN");
    }

    char *name = card->cid.name;
    uint32_t maxFreq = card->max_freq_khz;
    ESP_LOGI(TAG, "SD name: %s, maxFreq: %lu kHz", name, maxFreq);

    FATFS *fsinfo;
    DWORD fre_clust;
    uint64_t used = 0;
    uint64_t total = 0;
    if (f_getfree(MOUNT_POINT, &fre_clust, &fsinfo) == 0) {
        used = ((uint64_t)(fsinfo->csize)) * ((fsinfo->n_fatent - 2) - (fsinfo->free_clst)) * (fsinfo->ssize);
        total = ((uint64_t)(fsinfo->csize)) * (fsinfo->n_fatent - 2) * (fsinfo->ssize);
    } else {
        ESP_LOGW(TAG, "Failed to get drive status");
    }

    uint64_t space = ((uint64_t)card->csd.capacity) * ((uint64_t)card->csd.sector_size);
    ESP_LOGI(TAG, "SD Card Size: %llu MB", space / (1024) / 1024);
    ESP_LOGI(TAG, "Total space: %llu MB", total / (1024) / 1024);
    ESP_LOGI(TAG, "Used space: %llu kB", used / (1024));
}

void SdLogger::testSdCard() {
    // uint8_t cardType = SD.cardType();

    // if(cardType == CARD_NONE){
    //   ESP_LOGI(TAG, "No SD card attached");
    //   return;
    // }

    // ESP_LOGI(TAG, "SD Card Type: ");
    // if(cardType == CARD_MMC){
    //   ESP_LOGI(TAG, "MMC");
    // } else if(cardType == CARD_SD){
    //   ESP_LOGI(TAG, "SDSC");
    // } else if(cardType == CARD_SDHC){
    //   ESP_LOGI(TAG, "SDHC");
    // } else {
    //   ESP_LOGI(TAG, "UNKNOWN");
    // }

    // uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    // ESP_LOGI(TAG, "SD Card Size: %lluMB", cardSize);

    // // listDir(SD, "/", 0);
    // // createDir(SD, "/mydir");
    // // listDir(SD, "/", 0);
    // // removeDir(SD, "/mydir");
    // // listDir(SD, "/", 2);
    // // writeFile(SD, "/hello.txt", "Hello ");
    // // appendFile(SD, "/hello.txt", "World!\n");
    // // readFile(SD, "/hello.txt");
    // // deleteFile(SD, "/foo.txt");
    // // renameFile(SD, "/hello.txt", "/foo.txt");
    // // readFile(SD, "/foo.txt");
    // // testFileIO("/test.txt");
    // ESP_LOGI(TAG, "Total space: %lluMB", SD.totalBytes() / (1024 * 1024));
    // ESP_LOGI(TAG, "Used space: %lluMB", SD.usedBytes() / (1024 * 1024));
}

void SdLogger::testFileIO(const char * path) {
//   File file = SD.open(path);
//   static uint8_t buf[512];
//   size_t len = 0;
//   uint32_t start = millis();
//   uint32_t end = start;
//   if(file){
//       len = file.size();
//       size_t flen = len;
//       start = millis();
//       while(len){
//           size_t toRead = len;
//           if(toRead > 512){
//               toRead = 512;
//           }
//           file.read(buf, toRead);
//           len -= toRead;
//       }
//       end = millis() - start;
//       ESP_LOGI(TAG, "%u bytes read in %u ms", flen, end);
//       file.close();
//   } else {
//       ESP_LOGI(TAG, "Failed to open file for reading");
//   }


//   file = SD.open(path, FILE_WRITE);
//   if(!file){
//       ESP_LOGI(TAG, "Failed to open file for writing");
//       return;
//   }

//   size_t i;
//   start = millis();
//   for(i=0; i<2048; i++){
//       file.write(buf, 512);
//   }
//   end = millis() - start;
//   ESP_LOGI(TAG, "%u bytes written in %u ms", 2048 * 512, end);
//   file.close();
}

void SdLogger::log(const char *filePrefix, const char *record, bool createNew) {
    // char filename[128];
    // File file;

    // if(SD.cardType() == CARD_NONE) return;

    // long fileNumber = findLargestNumberInFilenames("/", filePrefix);
    // if(fileNumber < 0) {
    //     // Failed to access SD card, unmount it and try again later
    //     SD.end();
    // } else if(fileNumber == 0) {
    //     fileNumber = 1;
    //     snprintf(filename, 128, "/%s_%ld.json", filePrefix, fileNumber);
    //     file = SD.open(filename, FILE_WRITE);
    //     if(file) {
    //         ESP_LOGI(TAG, "File not found, created a new file; %s", filename);
    //     } else {
    //         ESP_LOGW(TAG, "Failed to create a new file; %s", filename);
    //     }
    // } else {
    //     snprintf(filename, 128, "/%s_%ld.json", filePrefix, fileNumber);
    //     file = SD.open(filename, FILE_APPEND);
    //     if(file) {
    //         if(createNew || file.size() > SD_FILE_MAX_SIZE) {
    //             file.close();
    //             fileNumber += 1;
    //             snprintf(filename, 128, "/%s_%ld.json", filePrefix, fileNumber);
    //             file = SD.open(filename, FILE_WRITE);
    //             if(file) {
    //                 ESP_LOGI(TAG, "Created a new file; %s", filename);
    //             } else {
    //                 ESP_LOGW(TAG, "Failed to create a new file; %s", filename);
    //             }
    //         } else {
    //             ESP_LOGI(TAG, "Opened existing file; %s", filename);
    //         }
    //     } else {
    //         ESP_LOGW(TAG, "Failed to open existing file; %s", filename);
    //     }
    // }

    // if(!file) {
    //     return;
    // }

    // size_t numWritten = file.write((uint8_t *)record, strlen(record));
    // file.close();
    // ESP_LOGI(TAG, "Wrote %lu bytes", numWritten);
}

long SdLogger::findLargestNumberInFilenames(const char* dir, const char* prefix) {
    // File root = SD.open(dir);
    // if (!root || !root.isDirectory()) {
    //     ESP_LOGI(TAG, "Failed to open directory: %s", dir);
    //     return -1;
    // }

    // long maxNum = 0;
    // File file = root.openNextFile();
    // while (file) {
    //     const char* name = file.name();
    //     size_t prefixLen = strlen(prefix);
    //     if (strncmp(name, prefix, prefixLen) == 0) {
    //         // Find the first sequence of digits after the prefix
    //         // +1 to skip the underscore
    //         const char* numStart = name + prefixLen + 1;
    //         char* endPtr;
    //         long num = strtol(numStart, &endPtr, 10);
    //         if (endPtr != numStart && num > maxNum) {
    //             maxNum = num;
    //         }
    //     }
    //     file = root.openNextFile();
    // }
    // root.close();
    // return maxNum;
    return 0;
}