#pragma once

#define UART_PROTCL_HEAD_OFFSET     0
#define UART_PROTCL_TYPE_OFFSET     1
#define UART_PROTCL_DATALEN_OFFSET  2
#define UART_PROTCL_DATA_OFFSET     4

#define UART_PROTCL_HEAD    (char)0xa5
#define UART_PROTCL_TAIL    (char)0xfc

#define UART_PROTCL_TYPE_NET            (char)0x0
#define UART_PROTCL_TYPE_GCODE          (char)0x1
#define UART_PROTCL_TYPE_FIRST          (char)0x2
#define UART_PROTCL_TYPE_FRAGMENT       (char)0x3
#define UART_PROTCL_TYPE_HOT_PORT       (char)0x4
#define UART_PROTCL_TYPE_STATIC_IP      (char)0x5

#define UART_FRAME_SIZE 1024

class MksSerialCom
{
public:
    MksSerialCom();
    void begin();
    void handle();
    bool sendNetworkInfos();
private:
    bool canSendFrame();
    void clearFrame();
    char _frame[UART_FRAME_SIZE];

};

extern MksSerialCom serialcom;
