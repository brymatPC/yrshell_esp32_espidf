#include "VictronDevice.h"
#include "UploadDataClient.h"
#include "SdLogger.h"
#include "Utilities.h"

#include <aes/esp_aes.h>
#include <psa/crypto.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log_custom.h"

typedef enum {
  STATE_RESET       = 0,
  STATE_IDLE        = 1,
  STATE_UPLOAD      = 2,
  STATE_UPLOAD_WAIT = 3,
  STATE_SEND_WAIT   = 4,
  STATE_WRITE_LOG   = 5,

} victronStates_t;

static const char* TAG = "Victron";

const char VictronDevice::s_PREF_NAMESPACE[] = "vic";
const unsigned int VictronDevice::s_UPLOAD_TIME_MS = 120000;
const unsigned int VictronDevice::s_STARTUP_OFFSET_MS = 5000;
char VictronDevice::s_ROUTE[] = "/victron";

static char charToHex( char c) {
    char value = '\0';
    if(  c >= '0' && c <= '9' ) {
        value |= c - '0';
    } else if(  c >= 'a' && c <= 'f' ) {
        value |= c - 'a' + 10;
    } else if(  c >= 'A' && c <= 'F') {
        value |= c - 'A' + 10;
    }
    return value;
}

VictronDevice::VictronDevice()  :
    m_uploadClient(nullptr),
    m_sdLogger(nullptr)
{
    memset(m_key, 0, VICTRON_KEY_LEN);
    m_bleData = bleDeviceData_t{};
    m_dataUploadReady = false;
    m_dataLogReady = false;
    m_lastUpdate = 0;
    m_state = STATE_RESET;
    m_timer.setInterval(s_STARTUP_OFFSET_MS);
    m_uploadRequest = false;
    m_numDuplicates = 0;
}
void VictronDevice::setup() {
    esp_err_t err;
    uint32_t _handle;
    size_t len = VICTRON_KEY_LEN;
    err = nvs_open(s_PREF_NAMESPACE, NVS_READONLY, &_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs partition, err: %lu", err);
        return;
    }
    m_key[0] = '\0';
    err = nvs_get_blob(_handle, "key", m_key, &len);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str fail: key - %lu", err);
    }
    nvs_close(_handle);
}
void VictronDevice::save() {
    esp_err_t err;
    uint32_t _handle;
    err = nvs_open(s_PREF_NAMESPACE, NVS_READWRITE, &_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs partition, err: %lu", err);
        return;
    }
    err = nvs_set_blob(_handle, "key", m_key, VICTRON_KEY_LEN);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str fail: key -  %lu", err);
    }
    err = nvs_commit(_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit fail: %lu", err);
    }
    nvs_close(_handle);
    ESP_LOGI(TAG, "Preferences updated");
}
void VictronDevice::setKey(const char *key) {
    uint8_t tempKey[VICTRON_KEY_LEN];
    uint8_t num = 0;
    bool keyValid = true;
    for(uint8_t i=0; i < VICTRON_KEY_LEN*2; i+=2) {
        if(key[i] == '\0' || key[i+1] == '\0') {
            keyValid = false;
            break;
        } else {
            tempKey[num] = charToHex(key[i]) << 4;
            tempKey[num] |= charToHex(key[i+1]);
            num++;
        }
    }
    if(keyValid) {
        memcpy(m_key, tempKey, VICTRON_KEY_LEN);
        ESP_LOGI(TAG, "key updated: key_0=0x%02X key_1=0x%02X", m_key[0], m_key[1]);
    } else {
        ESP_LOGI(TAG, "key invalid, ignoring: num=%u", num);
    }
}
void VictronDevice::parse() {
    if(m_bleData.payloadLen == 0) return;

    if(m_dataUploadReady && (HW_getMillis() < (m_lastUpdate + 30000))) {
        ESP_LOGD(TAG, "Probable duplicate: millis=%u m_lastUpdate=%u", (unsigned) HW_getMillis(), (unsigned)m_lastUpdate);
        m_numDuplicates++;
        return;
    }

    if(m_bleData.payloadLen > 7) {
        uint16_t companyId = (m_bleData.payload[1] << 8) | (m_bleData.payload[0]);
        uint8_t dataRecordType = m_bleData.payload[2];
        uint16_t modelId = (m_bleData.payload[4] << 8) | (m_bleData.payload[3]);
        uint8_t readOutType = m_bleData.payload[5];
        uint8_t recordType = m_bleData.payload[6];
        ESP_LOGI(TAG, "companyId=0x%04X, modelId=0x%04X, dataRecordType=0x%02X, readOutType=0x%02X, recordType=0x%02X", companyId, modelId, dataRecordType, readOutType, recordType);
    }

    if(m_bleData.payloadLen >= 25) {
        bool keyValid = false;
        for(uint8_t i=0; i < VICTRON_KEY_LEN; i++) {
            if(m_key[i] != 0) {
                keyValid = true;
                break;
            }
        }
        if(keyValid) {
            // payload 10-25 are the encrypted bytes
            decrypt();
        } else {
            ESP_LOGW(TAG, "key not valid, can't decrypt");
        }

    } else {
        ESP_LOGW(TAG, "insufficient bytes to parse: payloadLen=%u", m_bleData.payloadLen);
    }
}

void VictronDevice::decrypt() {
    uint8_t nonce_counter[16] = {0};
    uint8_t key_match = 0;
    uint8_t outputData[32] = {0};
    uint8_t inputData[32] = {0};
    size_t output_length = 0;
    uint32_t totalDecryptedBytes = 0;

    psa_status_t status;
    psa_key_id_t key_id;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;

    size_t dataLen = m_bleData.payloadLen - 10;

    // construct the 16-byte nonce counter array by piecing it together byte-by-byte.
    nonce_counter[0] = m_bleData.payload[7];
    nonce_counter[1] = m_bleData.payload[8];
    key_match = m_bleData.payload[9];
    memcpy(inputData, &m_bleData.payload[10], dataLen);

    ESP_LOGI(TAG, "KeyMatch: 0x%02X, Key: 0x%02X, Nonce: 0x%02X%02X, len: %u", key_match, m_key[0], m_bleData.payload[7], m_bleData.payload[8], dataLen);

    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "Failed to initialize PSA Crypto: %d", status);
        return;
    }

    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_CTR);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);

    status = psa_import_key(&attributes, m_key, VICTRON_KEY_LEN, &key_id);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "Failed to import key: %d", status);
        return;
    }
    status = psa_cipher_decrypt_setup(&operation, key_id, PSA_ALG_CTR);
    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "failed to setup decrypt: status=%d", status);
        psa_cipher_abort(&operation);
        return;
    }
    status = psa_cipher_set_iv(&operation, nonce_counter, 16);
    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "failed to set cipher iv: status=%d", status);
        psa_cipher_abort(&operation);
        return;
    }
    status = psa_cipher_update(&operation, inputData, dataLen, outputData, sizeof(outputData), &output_length);
    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "failed to update cypher: status=%d", status);
        psa_cipher_abort(&operation);
        return;
    }

    totalDecryptedBytes += output_length;

    status = psa_cipher_finish(&operation, outputData + output_length, sizeof(outputData) - output_length, &output_length);
    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "failed to finish cypher: status=%d", status);
        psa_cipher_abort(&operation);
        return;
    }

    totalDecryptedBytes += output_length;
    
    psa_destroy_key(key_id);
    ESP_LOGI(TAG, "Completed decryption of %lu bytes", totalDecryptedBytes);

    // Bits 15:0 - TTG (minutes)
    // Bits 31:16 - Battery voltage (0.01 V)
    // Bits 47:32 - Alarm Reason
    // Bits 63:48 - Aux voltage (0.01 V)
    // Bits 65:64 - Aux Input type
    // Bits 87:66 - Battery Current (0.001A)
    // Bits 107:88 - Consumed Ah (0.1 Ah)
    // Bits 117:108 - State of Charge (0.1%)
    uint16_t timeToGo = (outputData[1] << 8) | (outputData[0]);
    uint16_t batteryVoltage = (outputData[3] << 8) | (outputData[2]);
    // No alarms
    uint16_t auxVoltage = (outputData[7] << 8) | (outputData[6]);
    uint8_t auxType = (outputData[8] & 0x03);
    uint32_t batteryCurrent_u = (outputData[10] << 14) | (outputData[9] << 6) | ((outputData[8] & 0xFC) >> 2);
    uint32_t consumed = ((outputData[13] & 0x0F) << 16) | (outputData[12] << 8) | (outputData[11]);
    uint32_t stateOfCharge = ((outputData[14] & 0x3F) << 4) | ((outputData[13] & 0xF0) >> 4);
    int32_t batteryCurrent = 0;

    if(batteryCurrent_u <= 0x001FFFFF) {
        batteryCurrent = (int32_t) batteryCurrent_u;
    } else {
        batteryCurrent = (int32_t) (0x3FFFFF - batteryCurrent_u + 1);
        batteryCurrent *= -1;
    }

    ESP_LOGI(TAG, "addr=%s", m_bleData.addr);
    ESP_LOGI(TAG, "batteryVoltage=%u batteryCurrent=%d consumed=%lu", batteryVoltage, batteryCurrent, consumed);

    strncpy(m_data.serial, m_bleData.name, 32);
    m_data.timeToGo = timeToGo;
    m_data.batteryVoltage = batteryVoltage;
    m_data.batteryCurrent = batteryCurrent;
    m_data.stateOfCharge = stateOfCharge;
    m_dataUploadReady = true;
    m_dataLogReady = true;
    m_lastUpdate = HW_getMillis();

    #ifdef LOG_OUTPUT_DATA
        char outStr[128];
        outStr[0] = '0';
        outStr[1] = 'x';
        for(int i=0; i < 15; i++) {
            sprintf(&outStr[2+i*2], "%02X", outputData[i]);
        }
        ESP_LOGI(TAG, "outputData=%s", outStr);
    #endif
}
void VictronDevice::scanComplete() {
    m_uploadRequest = true;
    ESP_LOGI(TAG, "ScanComplete: m_numDuplicates=%u", m_numDuplicates);
    m_numDuplicates = 0;
}
void VictronDevice::toJson(char *buf, uint32_t maxLen, bool addLineEnding) {
    if(!buf) return;
    char ending[3] = "\r\n";
    if(!addLineEnding) {
        ending[0] = '\0';
    }
    char ts[51];
    getRtcTimeStr(ts, 51);
    snprintf(buf, maxLen, "{\"ts\":\"%s\",\"sn\":\"%s\",\"ttg\":%d,\"v\":%u,\"i\":%ld,\"soc\":%lu}%s",
             ts, m_data.serial, m_data.timeToGo, m_data.batteryVoltage, m_data.batteryCurrent, m_data.stateOfCharge, ending);
}
void VictronDevice::slice( void) {
    static bool firstRun = true;
    switch(m_state) {
        case STATE_RESET:
            if(m_timer.hasIntervalElapsed()) {
                m_timer.setInterval(s_UPLOAD_TIME_MS);
                m_state = STATE_IDLE;
            }
        break;
        case STATE_IDLE:
            if((m_timer.hasIntervalElapsed() || m_uploadRequest)) {
                m_timer.setInterval(s_UPLOAD_TIME_MS);
                m_uploadRequest = false;
                ESP_LOGD(TAG, "uploading data");
                m_state = STATE_UPLOAD;
            }
        break;
        case STATE_UPLOAD:
            if(m_dataUploadReady) {
                m_state = STATE_UPLOAD_WAIT;
            } else {
                ESP_LOGD(TAG, "No data to upload");
                m_state = STATE_WRITE_LOG;
            }
        break;
        case STATE_UPLOAD_WAIT:
            if(!m_uploadClient) {
                m_dataUploadReady = false;
                m_state = STATE_WRITE_LOG;
            } else if(!m_uploadClient->busy()) {
                toJson(m_sendBuf, MAX_VIC_SEND_BUF_SIZE, false);
                m_uploadClient->sendFile(s_ROUTE, m_sendBuf, strlen(m_sendBuf));
                m_dataUploadReady = false;
                m_state = STATE_SEND_WAIT;
            }
        break;
        case STATE_SEND_WAIT:
            if(!m_uploadClient->busy()) {
                ESP_LOGD(TAG, "upload complete");
                m_state = STATE_WRITE_LOG;
            }
        break;
        case STATE_WRITE_LOG:
            if(m_dataLogReady) {
                if(m_sdLogger) {
                    toJson(m_logBuf, MAX_VIC_SEND_BUF_SIZE, true);
                    m_sdLogger->log(TAG, m_logBuf, firstRun);
                    firstRun = false;
                }
                m_dataLogReady = false;
            }
            m_state = STATE_IDLE;
        break;
        default:
            m_state = STATE_RESET;
        break;
    }
}