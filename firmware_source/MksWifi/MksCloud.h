#pragma once

#include <ESP8266WiFi.h>

#define CLOUD_HOST "baizhongyun.cn"
#define CLOUD_PORT 12345

extern boolean cloud_enable_flag;
extern int cloud_link_state;
extern char unbind_exec;

typedef enum {
    CLOUD_NOT_CONNECT,
    CLOUD_IDLE,
    CLOUD_DOWNLOADING,
    CLOUD_DOWN_WAIT_M3,
    CLOUD_DOWNLOAD_FINISH,
    CLOUD_WAIT_PRINT,
    CLOUD_PRINTING,
    CLOUD_GET_FILE,
} CLOUD_STATE;


extern WiFiClient cloud_client;
