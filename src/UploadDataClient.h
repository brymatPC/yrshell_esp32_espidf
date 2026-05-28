#ifndef UPLOAD_DATA_CLIENT_H_
#define UPLOAD_DATA_CLIENT_H_

#include <Sliceable.h>

#define MAX_HEADER_BUF_SIZE 128
#define UDC_IP_LEN 16

class WifiConnection;

class UploadDataClient : public Sliceable {
private:
    static const char s_PREF_NAMESPACE[];

    bool m_connected;
    char m_ip[UDC_IP_LEN];
    uint32_t m_port;
    uint8_t m_state;
    bool m_sendRequest;
    char m_headerBuf[MAX_HEADER_BUF_SIZE];
    char *m_routeToSend;
    char *m_fileToSend;
    unsigned m_fileLength;

    int m_client;

    bool m_wifiConnected;
    char m_hostIp[UDC_IP_LEN];

    void changeState( uint8_t newState);
    void sendHeader();
    int32_t socketConnect();
public:
    UploadDataClient();
    virtual ~UploadDataClient();
    virtual const char* sliceName( ) { return "UploadDataClient"; }
    void init();
    virtual void slice( void);
    void setup();
    void save();
    void setHostIp(const char *ip);
    void setHostPort(unsigned port);
    void sendFile(char *route, char *file, unsigned len);
    bool busy();
    void updateWifiStatus(bool connected, const char *hostIp);
};

#endif // #ifndef UPLOAD_DATA_CLIENT_H_