#include "TelnetServer.h"

#include "esp_log_custom.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include <lwip/netdb.h>

static const char* TAG = "TelnetS";

typedef enum {
    STATE_STARTUP   = 10,
    STATE_IDLE      = 0,
    STATE_CONNECTED = 1,
    STATE_RECEIVE   = 2,
    STATE_CONVERT   = 3,
    STATE_CLOSE     = 14,
    STATE_FAULT     = 15,

} telnetServerStates_t;

void TelnetLogServer::init( unsigned port) {
    m_fromTelnetQ = &m_fq;
    m_toTelnetQ = &m_tq;
    m_port = port;
    m_configured = true;
    m_timer.setInterval( 10);
}

TelnetServer::TelnetServer() {
    m_fromTelnetQ = NULL;
    m_toTelnetQ = NULL;

    m_configured = false;
    m_listenSocket = -1;
    m_client = -1;
    m_data0 = m_data1 = 0;
    m_state = STATE_STARTUP;
    m_lastCharWasNull =  m_flipFlop = m_lastConnected =  false;
}

TelnetServer::~TelnetServer() {
    m_fromTelnetQ = NULL;
    m_toTelnetQ = NULL;
    m_state = STATE_STARTUP;
    m_listenSocket = -1;
    m_client = -1;
}

void TelnetServer::init( unsigned port, CircularQBase<char> *in, CircularQBase<char>* out) {
    m_fromTelnetQ = in;
    m_toTelnetQ = out;
    m_port = port;
    m_configured = true;
    m_timer.setInterval( 10);
}

bool TelnetServer::startListenSocket() {
    bool ret = false;
    m_listenSocket = socket(AF_INET6, SOCK_STREAM, 0);
    if(m_listenSocket < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return false;
    } 

    // opt = 1 means to enable the socket
    int opt = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // Set non-blocking mode
    int flags = fcntl(m_listenSocket, F_GETFL, 0);
    fcntl(m_listenSocket, F_SETFL, flags | O_NONBLOCK);
    ESP_LOGI(TAG, "Socket created");

    struct sockaddr_in6 dest_addr;
    bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
    dest_addr.sin6_addr.s6_addr[10] = 0xFF;
    dest_addr.sin6_addr.s6_addr[11] = 0xFF;
    memset(dest_addr.sin6_addr.s6_addr, 0x0, 16);
    dest_addr.sin6_family  = AF_INET6;
    dest_addr.sin6_port = htons(m_port);

    int err = bind(m_listenSocket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
    } else {
        ESP_LOGI(TAG, "Socket bound, port %d", m_port);
        err = listen(m_listenSocket, 1);
        if (err != 0) {
            ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        } else {
            ESP_LOGI(TAG, "Socket listening");
            ret = true;
        }
    }
    return ret;
}
void TelnetServer::configureClient() {
    if(m_client < 0) return;

    int keepAlive = 1;
    int keepIdle = 5;
    int keepInterval = 5;
    int keepCount = 3;
    // Set tcp keepalive option
    setsockopt(m_client, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
    setsockopt(m_client, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
    setsockopt(m_client, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
    setsockopt(m_client, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
}

void TelnetServer::changeState( uint8_t newState) {
    ESP_LOGI(TAG, "Change state from %u to %u", m_state, newState);
    m_state = newState;
}

void TelnetServer::slice() {
    if( m_timer.isNextInterval()) {
        uint32_t start = HW_getMicros();
        uint8_t startState = m_state;
        uint8_t data;

        if( m_lastConnected && m_client < 0) {
            ESP_LOGD(TAG, "Disconnect");
            m_lastConnected = false;
            changeState( STATE_IDLE);
        } else {
            switch( m_state) {
                case STATE_STARTUP:
                    // BAM - 20260107 - Need to wait for WiFi to be initialized before creating a server or client
                    if(m_configured) {
                        if(startListenSocket()) {
                            changeState( STATE_IDLE);
                        } else {
                            changeState( STATE_FAULT);
                        }
                    }
                break;
                case STATE_IDLE:
                {
                    struct sockaddr_storage source_addr;
                    socklen_t cs = sizeof(source_addr);
                    m_client = accept(m_listenSocket, (struct sockaddr *)&source_addr, (socklen_t *)&cs);
                    if(m_client >= 0) {
                        m_lastConnected = true;
                        configureClient();
                        ESP_LOGD(TAG, "Connected");
                        changeState( STATE_CONNECTED);
                    } else {
                        if(errno != EAGAIN && errno != EWOULDBLOCK) {
                            ESP_LOGW(TAG, "Error occurred during accept: errno %d", errno);
                        }
                    }
                }
                break;
                
                case STATE_CONNECTED:
                    if( m_flipFlop) {
                        m_flipFlop = false;
                        if( m_fromTelnetQ) {
                            if(m_fromTelnetQ->spaceAvailable()) {
                                int len = recv(m_client, &data, 1, MSG_DONTWAIT);
                                if (len < 0) {
                                    if(errno != EWOULDBLOCK) {
                                        ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                                        changeState( STATE_CLOSE);
                                    }
                                } else if (len > 0) {
                                    ESP_LOGD(TAG, "CharReceived: 0x%02X", data);
                                    if( data != 0xFF) {
                                        if( data || m_lastCharWasNull ) {
                                            m_fromTelnetQ->put( data);
                                            m_lastCharWasNull = false;
                                        } else {
                                            m_lastCharWasNull = true;
                                        }
                                    } else {
                                        m_data0 = 0xFF;
                                        changeState( STATE_RECEIVE);
                                    }
                                }
                            }
                        }
                    } else {
                        m_flipFlop = true;
                        if( m_toTelnetQ) {
                            const char* p = m_toTelnetQ->getLinearReadBuffer();
                            size_t len = m_toTelnetQ->getLinearReadBufferSize();
                            if( len > 0) {
                                size_t bw = send(m_client, (uint8_t*) p , len, MSG_DONTWAIT);
                                if( bw > 0) {
                                    m_toTelnetQ->drop( bw);
                                } else {
                                    ESP_LOGW(TAG, "Failed to send %lu bytes, errno: %d", len, errno);
                                }
                            }
                        }
                    }
                break;
                
                case STATE_RECEIVE:
                    if(m_fromTelnetQ->spaceAvailable()) {
                        int len = recv(m_client, &data, 1, 0);
                        if (len < 0) {
                            if(errno != EWOULDBLOCK) {
                                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                                changeState( STATE_CLOSE);
                            }
                        } else if (len > 0) {
                            if( data == 0xFF) {
                                m_fromTelnetQ->put( data);
                                changeState( STATE_CONNECTED);
                            } else {
                                m_data1 = data;
                                changeState( STATE_CONVERT);
                            }
                        }
                    }
                break;
                case STATE_CONVERT:
                    if(m_fromTelnetQ->spaceAvailable()) {
                        int len = recv(m_client, &data, 1, 0);
                        if (len < 0) {
                            if(errno != EWOULDBLOCK) {
                                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                                changeState( STATE_CLOSE);
                            }
                        } else if (len > 0) {
                            ESP_LOGV(TAG, "Request: m_data0=0x%02X, m_data1=0x%02X, data=0x%02X", m_data0, m_data1, data);
                            if( m_data1 == 0xFB && data == 0x03) {
                                m_data1 = 0xFD;
                            } else if( m_data1 == 0xFD && data == 0x03) {
                                m_data1 = 0xFB;
                            } else if( m_data1 == 0xFD && data == 0x01) {
                                m_data1 = 0xFB;
                            } else if( m_data1 == 0xFC) {
                                m_data1 = 0xFE;
                            } else if( m_data1 == 0xFE) {
                                m_data1 = 0xFB;
                            } else {
                                m_data0 = m_data1 = 0;
                            }
                            if( m_data0) {
                                send(m_client, &m_data0, 1, 0);
                                send(m_client, &m_data1, 1, 0);
                                send(m_client, &data, 1, 0);
                                ESP_LOGV(TAG, "Response: m_data0=0x%02X, m_data1=0x%02X, data=0x%02X", m_data0, m_data1, data);
                                m_data0 = m_data1 = 0;
                            }
                            changeState( STATE_CONNECTED);
                        }
                    }
                break;
                case STATE_CLOSE:
                    if(m_client >= 0) {
                        int err = close(m_client);
                        if(err) {
                            ESP_LOGW(TAG, "Failed to close client, err: %ld", err);
                        }
                        m_client = -1;
                    }
                    changeState( STATE_IDLE);
                break;
                case STATE_FAULT:
                    // Stay forever
                break;
            }
        }

        unsigned et =  HW_getMicros() - start;
        if( et > 900) {
            ESP_LOGD(TAG, "Slow slice: startState=%u, m_state=%u, time=%lu", startState, m_state, et);
        }
    }
}
