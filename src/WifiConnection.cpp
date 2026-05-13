#include "WifiConnection.h"
#include "esp_log_custom.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/inet.h"

#include <stdio.h>

#define BLINK_SPEED_CONNECTING_MS 200
#define BLINK_SPEED_SCANNING_MS   400
#define BLINK_SPEED_AP_MS         1000

#define WIFI_SSID "esp32"
#define WIFI_PASS "espPassword"

static const char* TAG = "WifiCon";

const char WifiConnection::s_PREF_NAMESPACE[] = "wifi";
const char WifiConnection::s_DEFAULT_HOST_NAME[] = "esp32";
const char WifiConnection::s_DEFAULT_HOST_PASSWORD[] = "espPassword";
// 192.168.10.2
const char WifiConnection::s_DEFAULT_HOST_IP[] = "0x020AA8C0";
// 192.168.10.1
const char WifiConnection::s_DEFAULT_HOST_GATEWAY[] = "0x010AA8C0";
const char WifiConnection::s_DEFAULT_HOST_MASK[] = "0x00FFFFFF";

typedef enum {
    STATE_RESET             = 0,
    STATE_INIT_CONNECT      = 1,
    STATE_LOAD_NETWORK_NAME = 2,
    STATE_WAIT_CONNECT      = 3,
    STATE_NEXT_NETWORK      = 4,
    STATE_CONNECTING        = 5,
    STATE_CONNECTED         = 6,
    STATE_INIT_AP           = 7,
    STATE_NETWORK_SCAN      = 8,
    STATE_PARSE_NETWORKS    = 9,
    STATE_CONFIGURE_AP      = 10,
    STATE_AP_READY          = 11,
    STATE_TURN_OFF          = 12,
    STATE_WAIT_OFF          = 13,
    STATE_OFF               = 14,
    STATE_IDLE              = 15,
} wifiStates_t;

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
static bool stringToUnsignedX( const char* P, uint32_t* V) {
    bool rc = false;
    uint32_t value = 0, numDigits = 0;
    if( (*P != '0') || (*(P+1) != 'x')) {
        rc = false;
    } else {
        P +=2;
        while( ((*P >= '0' && *P <= '9') ||  (*P >= 'a' && *P <= 'f')  ||  (*P >= 'A' && *P <= 'F'))  && numDigits++ < 16 ) {
            value <<= 4;
            value |= charToHex( *P++);
        }
        if( *P == '\0') {
            rc = true;
        }
        *V = value;
    }
    return rc;
}

static bool m_scanComplete = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d", MAC2STR(event->mac), event->aid, event->reason);
    } else if(event_id == WIFI_EVENT_SCAN_DONE) {
        uint16_t count = 0;
        esp_wifi_scan_get_ap_num(&count);
        ESP_LOGI(TAG, "Wifi Scan complete, found %u networks", count);
        m_scanComplete = true;
    }
}

WifiConnection::WifiConnection( LedDriver* led, uint32_t connectTimeout) {
    m_led = led;
    m_connectTimeout = connectTimeout;
    m_currentAp = 0;
    m_state = STATE_RESET;
    m_enable = false;
    m_requestOff = false;
    m_maxRssi = -1024;
    m_hostActive = false;

    m_hostName[0] = '\0';
    m_hostPassword[0] = '\0';
    m_hostIp[0] = '\0';
    m_hostGateway[0] = '\0';
    m_hostMask[0] = '\0';

    for(uint8_t i=0; i < MAX_WIFI_NETWORKS; i++) {
        m_networkName[i][0] = '\0';
        m_networkPassword[i][0] = '\0';
    }

    m_networkIp = 0;
    m_tryReconnect = false;
}

void WifiConnection::setup() {
    char parserKey[16];
    esp_err_t err;
    uint32_t _handle;
    size_t len = MAX_WIFI_ENTRY_LEN;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    err = nvs_open(s_PREF_NAMESPACE, NVS_READWRITE, &_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs partition, err: %lu", err);
        return;
    }
    strcpy(m_hostName, s_DEFAULT_HOST_NAME);
    err = nvs_get_str(_handle, "hName", m_hostName, &len);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str fail: hName - %lu", err);
    }
    strcpy(m_hostPassword, s_DEFAULT_HOST_PASSWORD);
    len = MAX_WIFI_ENTRY_LEN;
    err = nvs_get_str(_handle, "hPass", m_hostPassword, &len);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str fail: hPass - %lu", err);
    }
    strcpy(m_hostIp, s_DEFAULT_HOST_IP);
    len = MAX_WIFI_ENTRY_LEN;
    err = nvs_get_str(_handle, "hIp", m_hostIp, &len);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str fail: hIp - %lu", err);
    }
    strcpy(m_hostGateway, s_DEFAULT_HOST_GATEWAY);
    len = MAX_WIFI_ENTRY_LEN;
    err = nvs_get_str(_handle, "hGate", m_hostGateway, &len);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str fail: hGate - %lu", err);
    }
    strcpy(m_hostMask, s_DEFAULT_HOST_MASK);
    len = MAX_WIFI_ENTRY_LEN;
    err = nvs_get_str(_handle, "hMask", m_hostMask, &len);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str fail: hMask - %lu", err);
    }
    for(uint8_t i=0; i < MAX_WIFI_NETWORKS; i++) {
        snprintf(parserKey, 16, "nName%d", i);
        len = MAX_WIFI_ENTRY_LEN;
        err = nvs_get_str(_handle, parserKey, m_networkName[i], &len);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_str fail: %s - %lu", parserKey, err);
        }
        snprintf(parserKey, 16, "nPass%d", i);
        len = MAX_WIFI_ENTRY_LEN;
        err = nvs_get_str(_handle, parserKey, m_networkPassword[i], &len);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_str fail: %s - %lu", parserKey, err);
        }
    }
    nvs_close(_handle);
}
void WifiConnection::save() {
    char parserKey[16];
    esp_err_t err;
    uint32_t _handle;
    err = nvs_open(s_PREF_NAMESPACE, NVS_READWRITE, &_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs partition, err: %lu", err);
        return;
    }
    err = nvs_set_str(_handle, "hName", m_hostName);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str fail: hName - %lu", err);
    }
    err = nvs_set_str(_handle, "hPass", m_hostPassword);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str fail: hPass - %lu", err);
    }
    err = nvs_set_str(_handle, "hIp", m_hostIp);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str fail: hIp - %lu", err);
    }
    err = nvs_set_str(_handle, "hGate", m_hostGateway);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str fail: hGate - %lu", err);
    }
    err = nvs_set_str(_handle, "hMask", m_hostMask);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str fail: hMask - %lu", err);
    }
    for(uint8_t i=0; i < MAX_WIFI_NETWORKS; i++) {
        snprintf(parserKey, 16, "nName%d", i);
        err = nvs_set_str(_handle, parserKey, m_networkName[i]);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_str fail: %s -  %lu", parserKey, err);
        }
        snprintf(parserKey, 16, "nPass%d", i);
        err = nvs_set_str(_handle, parserKey, m_networkPassword[i]);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_str fail: %s -  %lu", parserKey, err);
        }
    }
    err = nvs_commit(_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit fail: %lu", err);
    }
    nvs_close(_handle);
    ESP_LOGI(TAG, "Preferences updated");
}
void WifiConnection::tryReconnect() {
    if(m_state == STATE_IDLE) {
        m_tryReconnect = true;
    }
}
const char* WifiConnection::getNetworkIp( void) {
    static char ipStr[MAX_WIFI_ENTRY_LEN];
    snprintf(ipStr, MAX_WIFI_ENTRY_LEN, "0x%08X", m_networkIp);
    return ipStr;
}
const char* WifiConnection::getNetworkName( uint8_t index) {
    if(index >= MAX_WIFI_NETWORKS) return nullptr;
    return m_networkName[index];
}
const char* WifiConnection::getNetworkPassword( uint8_t index) {
    if(index >= MAX_WIFI_NETWORKS) return nullptr;
    return m_networkPassword[index];
}
void WifiConnection::setHostName( const char* networkName) {
    strncpy(m_hostName, networkName, MAX_WIFI_ENTRY_LEN);
}
void WifiConnection::setHostPassword( const char* networkPassword ) {
    strncpy(m_hostPassword, networkPassword, MAX_WIFI_ENTRY_LEN);
}
void WifiConnection::setHostIp( const char* ip ) {
    strncpy(m_hostIp, ip, MAX_WIFI_ENTRY_LEN);
}
void WifiConnection::setHostGateway( const char* gateway) {
    strncpy(m_hostGateway, gateway, MAX_WIFI_ENTRY_LEN);
}
void WifiConnection::setHostMask( const char* mask) {
    strncpy(m_hostMask, mask, MAX_WIFI_ENTRY_LEN);
}
void WifiConnection::setNetworkName( uint8_t index, const char* networkName) {
    if(index >= MAX_WIFI_NETWORKS) return;
    strncpy(m_networkName[index], networkName, MAX_WIFI_ENTRY_LEN);
}
void WifiConnection::setNetworkPassword( uint8_t index, const char* networkPassword) {
    if(index >= MAX_WIFI_NETWORKS) return;
    strncpy(m_networkPassword[index], networkPassword, MAX_WIFI_ENTRY_LEN);
}
void WifiConnection::changeState( uint8_t state) {
    ESP_LOGD(TAG, "Change state from %u to %u", m_state, state);
    m_state = state;
}

int WifiConnection::getConnectedNetworkIndex( void) {
     return isNetworkConnected() ? m_currentAp : -1;
}

bool WifiConnection::isNetworkConnected( void) {
    //return WiFi.status() == WL_CONNECTED;
    return false;
}

void WifiConnection::slice( ) {
  const char* p;
  const char* q;
  int i, k, m;
  esp_err_t err;
  switch( m_state) {
    case STATE_RESET:
        m_currentAp = 0;
        if( m_led) {
            m_led->off();
        }
        if(m_enable) {
            changeState( STATE_INIT_AP);
        }
    break;

    // case STATE_INIT_CONNECT:
    //   if( m_led) {
    //     m_led->off();
    //   }
    //   if( m_enable && getNumberOfNetworks() >= 0) {
    //     m_timer.setInterval( m_connectTimeout);
    //     changeState( STATE_LOAD_NETWORK_NAME);
    //   }
    // break;

    // case STATE_LOAD_NETWORK_NAME:
    //   if( m_led) {
    //     m_led->blink( BLINK_SPEED_CONNECTING_MS);
    //   }
    //   p = getNetworkName( m_currentAp );
    //   q = getNetworkPassword( m_currentAp );
    //   ESP_LOGI(TAG, "Configured Network: %u, %s, %s", m_currentAp, p, q);
    //   if( *p == '\0' || *q == '\0') {
    //     ESP_LOGD(TAG, "Network not configured, going to wait state");
    //     changeState( STATE_WAIT_CONNECT);
    //   } else {
    //     WiFi.begin( (char*)p,  q);
    //     changeState( STATE_WAIT_CONNECT);
    //     ESP_LOGD(TAG, "Connecting %s", p);
    //   }
    // break;

    // case STATE_WAIT_CONNECT:
    //   if( WiFi.status() == WL_CONNECTED) {
    //     m_networkIp = (uint32_t) WiFi.localIP();
    //     changeState( STATE_CONNECTING);
    //   } else if(m_requestOff) {
    //     changeState( STATE_TURN_OFF);
    //   } else if( m_timer.hasIntervalElapsed()) {
    //     WiFi.disconnect(false);
    //     changeState( STATE_NEXT_NETWORK);
    //   } 
    // break;

    // case STATE_NEXT_NETWORK:
    //   m_currentAp++;
    //   if( m_currentAp >= getNumberOfNetworks() ) {
    //     m_currentAp = 0;
    //     ESP_LOGW(TAG, "Failed to connect to WiFi Station, reverting to Access Point: %s", m_hostName);
    //     changeState( STATE_CONFIGURE_AP);
    //   } else {
    //     changeState( STATE_INIT_CONNECT);
    //   }
    // break;

    // case STATE_CONNECTING:
    //   p = getNetworkName( m_currentAp );
    //   q = getNetworkIp();
    //   ESP_LOGI(TAG, "Connected %s, %s", p, q);
    //   m_timer.setInterval( 500);
    //   changeState( STATE_CONNECTED);
    //   if( m_led) {
    //     m_led->on();
    //   }
    // break;

    // case STATE_CONNECTED:
    //   if(m_requestOff) {
    //     changeState( STATE_TURN_OFF);
    //   } else if( m_timer.isNextInterval() ) {
    //     if(WiFi.status() != WL_CONNECTED) {
    //       ESP_LOGI(TAG, "Disconnected %s", getNetworkName( m_currentAp ));
    //       changeState( STATE_NEXT_NETWORK);
    //     }
    //   }
    // break;

    case STATE_INIT_AP:
        err = esp_wifi_disconnect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "STA disconnect failed! %lu", err);
        }
        configBasicAp();
        m_timer.setInterval( 100);
        changeState( STATE_NETWORK_SCAN);
        m_maxRssi = -1024;
        m_maxRssiIndex = 0;
        if( m_led) {
            m_led->blink( BLINK_SPEED_SCANNING_MS);
        }
    break;

    case STATE_NETWORK_SCAN:
        if( m_timer.hasIntervalElapsed() ) {
            startScan();
            m_timer.setInterval( m_connectTimeout * 2);
            ESP_LOGI(TAG, "Start scan");
            changeState( STATE_PARSE_NETWORKS);
        }
    break;

    case STATE_PARSE_NETWORKS:
        if( m_scanComplete) {
            uint16_t apCount;
            wifi_ap_record_t ap_info;
            m = getNumberOfNetworks();
            ESP_LOGI(TAG, "Scan complete");
            esp_wifi_scan_get_ap_num(&apCount);
            ESP_LOGI(TAG, "Parse %d, %d network", apCount, m);
            for( int j = 0; j < apCount; j++ ) {
                esp_wifi_scan_get_ap_record(&ap_info);
                ESP_LOGI(TAG, "RSSI %d, network %s", ap_info.rssi, ap_info.ssid);
                // for( k = 0; k < m; k++ ) {
                //   if( !strcmp( WiFi.SSID( j).c_str(), getNetworkName( k)) ) {
                //     int32_t RSSI;
                //     RSSI = WiFi.RSSI( j);
                //     if( RSSI > m_maxRssi) {
                //       m_maxRssi = RSSI;
                //       m_maxRssiIndex = k;
                //     }
                //     ESP_LOGI(TAG, "Max RSSI index %d, max RSSI %d, network %s", m_maxRssiIndex, 0 - m_maxRssi, WiFi.SSID( j).c_str());
                //   }
                // }
            }
            esp_wifi_clear_ap_list();
            //changeState( STATE_INIT_CONNECT);
            changeState( STATE_CONFIGURE_AP);
        } else if( m_timer.hasIntervalElapsed()) {
            ESP_LOGW(TAG, "Scan timeout");
            //changeState( STATE_INIT_CONNECT);
            changeState( STATE_CONFIGURE_AP);
      }
    break;

    case STATE_CONFIGURE_AP:
        p = m_hostName;
        q = m_hostPassword;
        ESP_LOGI(TAG, "Host: %s, %s", p, q);
        if( *p == '\0' || *q == '\0') {
            ESP_LOGI(TAG, "No Host: %s, %s", p, q);
        } else {
            esp_netif_ip_info_t ip;
            hostConfig( );
            configStation();
            m_hostActive = true;
            ESP_LOGI(TAG, "Host Up: %s, %s", p, q);
            if(esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip) == ESP_OK) {
                char ip_addr[16];
                inet_ntoa_r(ip.ip.addr, ip_addr, 16);
                ESP_LOGI(TAG, "Host: IP %s", ip_addr);
            } else {
                ESP_LOGW(TAG, "Failed to get ap ip address");
            }
            
        }
        changeState( STATE_AP_READY);
    break;

    case STATE_AP_READY:
        m_currentAp = m_maxRssiIndex == 0 ? 0 : m_maxRssiIndex;
        ESP_LOGI(TAG, "AP ready: %u, %u, %d", m_enable,  m_currentAp, getNumberOfNetworks());
        changeState( STATE_IDLE);
        if( m_led) {
            m_led->blink( BLINK_SPEED_AP_MS);
        }
    break;
    case STATE_IDLE:
      if(m_tryReconnect) {
            m_tryReconnect = false;
            // m_currentAp = 0;
            // ESP_LOGI(TAG, "Reconnect requested, retrying configured networks");
            // changeState( STATE_INIT_CONNECT);
      }
    break;
    // case STATE_TURN_OFF:
    //   if(WiFi.status() == WL_CONNECTED) {
    //       WiFi.disconnect();
    //   }
    //   if(m_hostActive) {
    //     WiFi.softAPdisconnect();
    //     m_hostActive = false;
    //   }

    //   changeState( STATE_WAIT_OFF);
    // break;
    // case STATE_WAIT_OFF:
    //   if(WiFi.status() == WL_DISCONNECTED || WiFi.status() == WL_STOPPED) {
    //       ESP_LOGI(TAG, "WIFI off");
    //       changeState( STATE_OFF);
    //   }
    // break;
    case STATE_OFF:
        m_requestOff = false;
        // Wait for reboot or wake up
    break;
    default:
        ESP_LOGW(TAG, "Invalid state: %u", m_state);
        changeState( STATE_RESET);
    break;
  }
}
void WifiConnection::off() {
    if(m_state != STATE_OFF) {
         m_requestOff = true;
    }
}
bool WifiConnection::isOff() {
    return m_state == STATE_OFF;
}
void WifiConnection::hostConfig( ) {
    uint32_t v;
    uint32_t ip = 0;
    uint32_t gw = 0;
    uint32_t mask = 0;

    if( stringToUnsignedX( m_hostIp, &v)) {
        ip = v;
    }
    if( stringToUnsignedX( m_hostGateway, &v)) {
        gw = v;
    }
    if( stringToUnsignedX( m_hostMask, &v)) {
        mask = v;
    }
    // if( !WiFi.softAPConfig( IPAddress(ip), IPAddress(gw) , IPAddress( mask))) {
    //   ESP_LOGW(TAG, "Host config failed");
    // }
}

void WifiConnection::configBasicAp() {

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

}
void WifiConnection::configStation(void)
{
    wifi_config_t wifi_config = {
        .ap = {
            .channel = 1,
            .authmode = WIFI_AUTH_WPA3_PSK,
            .max_connection = 3,
            .pmf_cfg = {
                .required = true,
            },
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .gtk_rekey_interval = 600,
        },
    };
    size_t ssidLen = strlen(m_hostName);
    memcpy(wifi_config.ap.ssid, m_hostName, ssidLen);
    wifi_config.ap.ssid_len = ssidLen;
    memcpy(wifi_config.ap.password, m_hostPassword, strlen(m_hostPassword));

    if (strlen(m_hostPassword) == 0 && wifi_config.ap.authmode != WIFI_AUTH_OWE) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:1", m_hostName, m_hostPassword);
}
void WifiConnection::startScan() {
    wifi_scan_config_t config;
    memset(&config, 0, sizeof(wifi_scan_config_t));
    config.channel = 0;
    config.show_hidden = false;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min = 100;
    config.scan_time.active.max = 300;
    esp_err_t err = esp_wifi_scan_start(&config, false);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start wifi scan, err: %lu", err);
    } else {
        m_scanComplete = false;
    }
}