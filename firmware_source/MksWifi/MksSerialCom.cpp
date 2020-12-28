#include "Config.h"
#include <ESP8266WiFi.h>
#include "MksSerialCom.h"
#include "MksCloud.h"
#include "MksTCPServer.h"


#define FRAME_WAIT_TO_SEND_TIMEOUT  2000
#define NET_FRAME_REFRESH_TIME  10000

extern OperatingState currentState;
extern char moduleId[21];
extern char softApName[32];
extern char softApKey[64];
extern char ssid[32];
extern char pass[64];
extern bool verification_flag;

MksSerialCom serialcom;

MksSerialCom::MksSerialCom() {}
void MksSerialCom::begin() {}
void MksSerialCom::handle()
{
    static uint32_t networkFrameTimeout = millis();
    //need to send periodically the network status
    if ((millis() - networkFrameTimeout) > NET_FRAME_REFRESH_TIME) {
        sendNetworkInfos();
        networkFrameTimeout = millis();
    }
}

void MksSerialCom::clearFrame()
{
    log_mkswifi("Clear buffer");
    memset(_frame, 0, sizeof(_frame));
}

bool MksSerialCom::sendNetworkInfos()
{
    if (canSendFrame()) {
        log_mkswifi("Can send frame");
        clearFrame();
        uint frameSize;
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

        frameSize = dataLen + 5;
        if (Serial.write(_frame, frameSize) == frameSize) {
            ;
            return true;
        } else {
            log_mkswifi("Send frame failed");
            return false;
        }
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
