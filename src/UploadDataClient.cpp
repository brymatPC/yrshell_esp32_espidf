#include "UploadDataClient.h"

#include "esp_log_custom.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include <lwip/netdb.h>

static const char* TAG = "Upload ";

typedef enum {
  STATE_STARTUP         = 0,
  STATE_RESET           = 1,
  STATE_IDLE            = 2,
  STATE_CONNECTING      = 3,
  STATE_CONNECTED       = 4,
  STATE_DISCONNECTING   = 5,
  STATE_SEND_FILE       = 6,

} ClientStates_t;

const char UploadDataClient::s_PREF_NAMESPACE[] = "udc";

UploadDataClient::UploadDataClient() {
    m_connected = false;
    m_ip[0] = '\0';
    m_port = 0;
    m_state = STATE_STARTUP;
    m_client = -1;
    m_wifiConnected = false;
    m_hostIp[0] = '\0';
}

UploadDataClient::~UploadDataClient( void) {
}

void UploadDataClient::init() {
}
void UploadDataClient::setup() {
    esp_err_t err;
    uint32_t _handle;
    size_t len = UDC_IP_LEN;
    err = nvs_open(s_PREF_NAMESPACE, NVS_READONLY, &_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs partition, err: %lu", err);
        return;
    }

    m_ip[0] = '\0';
    err = nvs_get_str(_handle, "ip", m_ip, &len);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str fail: ip - %lu", err);
    }
    m_port = 0;
    err = nvs_get_u32(_handle, "port", &m_port);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_u32 fail: port -  %lu", err);
    }
    nvs_close(_handle);
}
void UploadDataClient::save() {
    esp_err_t err;
    uint32_t _handle;
    err = nvs_open(s_PREF_NAMESPACE, NVS_READWRITE, &_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs partition, err: %lu", err);
        return;
    }
    err = nvs_set_str(_handle, "ip", m_ip);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str fail: ip -  %lu", err);
    }
    err = nvs_set_u32(_handle, "port", m_port);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u32 fail: port -  %lu", err);
    }
    err = nvs_commit(_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit fail: %lu", err);
    }
    nvs_close(_handle);
    ESP_LOGI(TAG, "Preferences updated");
}
void UploadDataClient::setHostIp(const char *ip) {
    strncpy(m_ip, ip, UDC_IP_LEN);
}
void UploadDataClient::setHostPort(unsigned port) {
    m_port = port;
}
bool UploadDataClient::busy() {
    return m_state != STATE_IDLE || m_sendRequest;
}
void UploadDataClient::updateWifiStatus(bool connected, const char *hostIp) {
    m_wifiConnected = connected;
    strncpy(m_hostIp, hostIp, UDC_IP_LEN);
}
void UploadDataClient::changeState( uint8_t newState) {
    ESP_LOGI(TAG, "change state from %u to %u", m_state, newState);
    m_state = newState;
}
void UploadDataClient::sendHeader() {
    if(m_client) {
        snprintf(m_headerBuf, MAX_HEADER_BUF_SIZE, "POST %s HTTP/1.1\r\nHost: %s:%lu\r\nContent-type: application/json\r\nContent-Length: %d\r\n\r\n",
                m_routeToSend, m_hostIp, m_port, m_fileLength);
        send(m_client, m_headerBuf, strlen(m_headerBuf), 0);
    }
}
void UploadDataClient::sendFile(char *route, char *file, unsigned len) {
    ESP_LOGI(TAG, "File len: %u", len);
    if(route != nullptr && file != nullptr && len > 0) {
        m_routeToSend = route;
        m_fileToSend = file;
        m_fileLength = len;
        m_sendRequest = true;
    }
}
int32_t UploadDataClient::socketConnect() {
    int addr_family = 0;
    int ip_protocol = 0;
    struct sockaddr_in dest_addr;
    inet_pton(AF_INET, m_ip, &dest_addr.sin_addr);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(m_port);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    m_client =  socket(addr_family, SOCK_STREAM, ip_protocol);
    if (m_client < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        m_client = -1;
        return -1;
    }
    ESP_LOGI(TAG, "Socket created, connecting to %s:%d", m_ip, m_port);

    int err = connect(m_client, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
        m_client = -1;
        return -1;
    }
    ESP_LOGI(TAG, "Successfully connected");
    return ESP_OK;
}
void UploadDataClient::slice() {
    switch( m_state) {
        case STATE_STARTUP:
            if(m_ip[0] != '\0' && m_port != 0) {
                changeState( STATE_IDLE);
            }
        break;
        case STATE_IDLE:
            if(m_sendRequest) {
                m_sendRequest = false;
                changeState( STATE_CONNECTING);
            }
        break;
        case STATE_CONNECTING:
        {
            int32_t ret = socketConnect();
            if(ret != ESP_OK) {
                ESP_LOGI(TAG, "Connect failed: %d", ret);
            }
            if(m_client >= 0) {
                changeState( STATE_CONNECTED);
            } else {
                changeState( STATE_DISCONNECTING);
            }
        }
        break;
        case STATE_CONNECTED:
            sendHeader();
            changeState( STATE_SEND_FILE);
        break;
        case STATE_SEND_FILE:
            send(m_client, m_fileToSend, m_fileLength, 0);
            send(m_client, "\r\n", 2, 0);
            changeState( STATE_DISCONNECTING);
        break;
        case STATE_DISCONNECTING:
            shutdown(m_client, 0);
            close(m_client);
            ESP_LOGD(TAG, "Done");
            changeState( STATE_IDLE);
        break;
    }
}
