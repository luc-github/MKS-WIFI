#include "Config.h"
#include <ESP8266WiFi.h>
#include "MksSerialCom.h"
#include "MksCloud.h"
#include "MksTCPServer.h"


#define FRAME_WAIT_TO_SEND_TIMEOUT  2000
#define NET_FRAME_REFRESH_TIME  10000

#define ESP_COMMUNICATION_BR  115200
#define ESP_TRANSFER_BR  1958400

extern OperatingState currentState;
extern TRANS_STATE transfer_state;
extern char moduleId[21];
extern char softApName[32];
extern char softApKey[64];
extern char ssid[32];
extern char pass[64];
extern bool verification_flag;
extern uint32_t ip_static;
extern uint32_t subnet_static;
extern uint32_t gateway_static;
extern uint32_t dns_static;

MksSerialCom serialcom;

MksSerialCom::MksSerialCom()
{
    _frameSize=0;
}
void MksSerialCom::begin()
{
    Serial.begin(ESP_COMMUNICATION_BR);
}

void MksSerialCom::handle()
{
}

bool MksSerialCom::transferFragment()
{
    bool res = false;
    if (Serial.write(_frame, _frameSize) == _frameSize) {
        log_mkswifi("Send frame Ok");
        res = true;
    } else {
        log_mkswifi("Send frame failed");
    }
    clearFrame();
    return res;
}

void MksSerialCom::clearFrame()
{
    log_mkswifi("Clear buffer");
    memset(_frame, 0, sizeof(_frame));
}

void MksSerialCom::communicationMode()
{
    if(Serial.baudRate() != ESP_COMMUNICATION_BR) {
        Serial.flush();
        Serial.begin(ESP_COMMUNICATION_BR);
    }
}
void MksSerialCom::transferMode()
{
    if(Serial.baudRate() != ESP_TRANSFER_BR) {
        Serial.flush();
        Serial.begin(ESP_TRANSFER_BR);
    }
}

void MksSerialCom::StaticIPInfosFragment()
{
    static uint32_t staticFrameTimeout = millis();
    if ((millis() - staticFrameTimeout) > NET_FRAME_REFRESH_TIME + 2) {
        staticFrameTimeout = millis();

        log_mkswifi("Can send Static frame");
        clearFrame();
        _frame[UART_PROTCL_HEAD_OFFSET] = UART_PROTCL_HEAD;
        _frame[UART_PROTCL_TYPE_OFFSET] = UART_PROTCL_TYPE_STATIC_IP;

        _frame[UART_PROTCL_DATA_OFFSET] = ip_static & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 1] = (ip_static >> 8) & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 2] = (ip_static >> 16) & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 3] = (ip_static >> 24) & 0xff;

        _frame[UART_PROTCL_DATA_OFFSET + 4] = subnet_static & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 5] = (subnet_static >> 8) & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 6] = (subnet_static >> 16) & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 7] = (subnet_static >> 24) & 0xff;

        _frame[UART_PROTCL_DATA_OFFSET + 8] = gateway_static & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 9] = (gateway_static >> 8) & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 10] = (gateway_static >> 16) & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 11] = (gateway_static >> 24) & 0xff;

        _frame[UART_PROTCL_DATA_OFFSET + 12] = dns_static & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 13] = (dns_static >> 8) & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 14] = (dns_static >> 16) & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 15] = (dns_static >> 24) & 0xff;

        _frame[UART_PROTCL_DATALEN_OFFSET] = 16;
        _frame[UART_PROTCL_DATALEN_OFFSET + 1] = 0;

        _frame[UART_PROTCL_DATA_OFFSET + 16] = UART_PROTCL_TAIL;

        _frameSize = UART_PROTCL_DATA_OFFSET + 17;



    } else {
        log_mkswifi("Not in timeout");
    }
}

void MksSerialCom::NetworkInfosFragment(bool force)
{
    static uint32_t networkFrameTimeout = millis();
    //need to send periodically the network status
    if (((millis() - networkFrameTimeout) > NET_FRAME_REFRESH_TIME) || force) {
        networkFrameTimeout = millis();
        clearFrame();
        int dataLen;
        int wifi_name_len = 0;
        int wifi_key_len = 0;
        int host_len = strlen(CLOUD_HOST);
        log_mkswifi("Set frame header");
        _frame[UART_PROTCL_HEAD_OFFSET] = UART_PROTCL_HEAD;
        _frame[UART_PROTCL_TYPE_OFFSET] = UART_PROTCL_TYPE_NET;

        if(currentState == OperatingState::Client) {
            log_mkswifi("STA Mode");
            if(WiFi.status() == WL_CONNECTED) {
                log_mkswifi("Connected : %s", WiFi.localIP().toString().c_str());
                _frame[UART_PROTCL_DATA_OFFSET] = WiFi.localIP()[0];
                _frame[UART_PROTCL_DATA_OFFSET + 1] = WiFi.localIP()[1];
                _frame[UART_PROTCL_DATA_OFFSET + 2] = WiFi.localIP()[2];
                _frame[UART_PROTCL_DATA_OFFSET + 3] = WiFi.localIP()[3];
                _frame[UART_PROTCL_DATA_OFFSET + 6] = 0x0a;
            } else {
                log_mkswifi("Not Connected");
                _frame[UART_PROTCL_DATA_OFFSET] = 0;
                _frame[UART_PROTCL_DATA_OFFSET + 1] = 0;
                _frame[UART_PROTCL_DATA_OFFSET + 2] = 0;
                _frame[UART_PROTCL_DATA_OFFSET + 3] = 0;
                _frame[UART_PROTCL_DATA_OFFSET + 6] = 0x05;
            }

            _frame[UART_PROTCL_DATA_OFFSET + 7] = 0x02;

            wifi_name_len = strlen(ssid);
            wifi_key_len = strlen(pass);

            _frame[UART_PROTCL_DATA_OFFSET + 8] = wifi_name_len;

            strcpy(&_frame[UART_PROTCL_DATA_OFFSET + 9], ssid);

            _frame[UART_PROTCL_DATA_OFFSET + 9 + wifi_name_len] = wifi_key_len;

            strcpy(&_frame[UART_PROTCL_DATA_OFFSET + 10 + wifi_name_len], pass);


        } else if(currentState == OperatingState::AccessPoint) {
            log_mkswifi("AP Mode: %s", WiFi.softAPIP().toString().c_str());
            _frame[UART_PROTCL_DATA_OFFSET] = WiFi.softAPIP()[0];
            _frame[UART_PROTCL_DATA_OFFSET + 1] = WiFi.softAPIP()[1];
            _frame[UART_PROTCL_DATA_OFFSET + 2] = WiFi.softAPIP()[2];
            _frame[UART_PROTCL_DATA_OFFSET + 3] = WiFi.softAPIP()[3];

            _frame[UART_PROTCL_DATA_OFFSET + 6] = 0x0a;
            _frame[UART_PROTCL_DATA_OFFSET + 7] = 0x01;


            wifi_name_len = strlen(softApName);
            wifi_key_len = strlen(softApKey);
            log_mkswifi("SSID (%d): %s, PWD (%d):%s",wifi_name_len,softApName, wifi_key_len,softApKey);
            _frame[UART_PROTCL_DATA_OFFSET + 8] = wifi_name_len;
            strcpy(&_frame[UART_PROTCL_DATA_OFFSET + 9], softApName);
            _frame[UART_PROTCL_DATA_OFFSET + 9 + wifi_name_len] = wifi_key_len;
            strcpy(&_frame[UART_PROTCL_DATA_OFFSET + 10 + wifi_name_len], softApKey);
        }

        if(cloud_enable_flag) {
            log_mkswifi("Cloud service is enabled");
            if(cloud_link_state == 3) {
                _frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 10] =0x12;
            } else if( (cloud_link_state == 1) || (cloud_link_state == 2)) {
                _frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 10] =0x11;
            } else if(cloud_link_state == 0) {
                _frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 10] =0x10;
            }
        } else {
            log_mkswifi("Cloud service is disabled");
            _frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 10] =0x0;

        }

        log_mkswifi("Cloud Host (%d): %s, port: %d", host_len, CLOUD_HOST, CLOUD_PORT);
        _frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 11] = host_len;
        strcpy(&_frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 12], CLOUD_HOST);
        _frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + 12] = CLOUD_PORT & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + 13] = (CLOUD_PORT >> 8 ) & 0xff;


        int id_len = strlen(moduleId);
        log_mkswifi("ModuleID (%d): %s", id_len, moduleId);
        _frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + 14]  = id_len;
        strcpy(&_frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + 15], moduleId);

        int ver_len = strlen(FW_VERSION);
        log_mkswifi("FW (%d): %s", ver_len, FW_VERSION);
        _frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + id_len + 15]  = ver_len;
        strcpy(&_frame[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + id_len + 16], FW_VERSION);

        dataLen = wifi_name_len + wifi_key_len + host_len + id_len + ver_len + 16;
        log_mkswifi("Cloud service Port: %d", TcpServer.port());
        _frame[UART_PROTCL_DATA_OFFSET + 4] = TcpServer.port() & 0xff;
        _frame[UART_PROTCL_DATA_OFFSET + 5] = (TcpServer.port() >> 8 )& 0xff;

        if(!verification_flag) {
            _frame[UART_PROTCL_DATA_OFFSET + 6] = 0x0e;
            log_mkswifi("Exception state");
        }

        log_mkswifi("Data len: %d", dataLen);
        _frame[UART_PROTCL_DATALEN_OFFSET] = dataLen & 0xff;
        _frame[UART_PROTCL_DATALEN_OFFSET + 1] = (dataLen >> 8 )& 0xff;


        _frame[dataLen + 4] = UART_PROTCL_TAIL;

        _frameSize = dataLen + 5;
    } else {
        log_mkswifi("Not in timeout");
    }
}

bool MksSerialCom::sendNetworkInfos()
{
    if (canSendFrame()) {
        log_mkswifi("Can send Network frame");
        NetworkInfosFragment(true);
        return transferFragment();
    } else {
        log_mkswifi("Cannot send frame");
        return false;
    }
}

bool MksSerialCom::canSendFrame()
{
    log_mkswifi("Is board ready for frame?");
    digitalWrite(EspReqTransferPin, HIGH);
    uint32_t startTime = millis();
    while( (millis() - startTime) <  FRAME_WAIT_TO_SEND_TIMEOUT) {
        if (digitalRead(McuTfrReadyPin) == LOW) {
            log_mkswifi("Yes");
            return true;
        }
    }
    log_mkswifi("Time out no board answer");
    return false;
}
