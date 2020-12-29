#include "Config.h"
#include <ESP8266WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESP8266HTTPClient.h>
#include "MksEEPROM.h"
#include "MksCloud.h"
#include "MksHTTPServer.h"
#include "MksTCPServer.h"
#include "MksSerialCom.h"
#include "MksNodeMonitor.h"
#include "MksFiFoFile.h"
#include "gcode.h"

//define
#define MAX_WIFI_FAIL 50
#define QUEUE_MAX_NUM    10

#define TCP_FRAG_LEN    1400

#define FILE_BLOCK_SIZE (1024 - 5 - 4)
char cmd_fifo[100] = {0};
uint cmd_index = 0;

//Variable
char M3_TYPE = TFT28;
boolean GET_VERSION_OK = false;
char wifi_mode[15] = {0};
char moduleId[21] = {0};
char softApName[32]= {0};
char softApKey[64] = {0};
char ssid[32] = {0};
char pass[64] = {0};
char webhostname[64]= {0};
uint8_t manual_valid = 0xff; //whether it use static ip
uint32_t ip_static;
uint32_t subnet_static;
uint32_t gateway_static;
uint32_t dns_static;
bool verification_flag = false;


String monitor_rx_buf = "";

char uart_send_package[1024];
uint32_t uart_send_size;

char uart_send_package_important[1024]; //for the message that cannot missed
uint32_t uart_send_length_important;

bool upload_error = false;
bool upload_success = false;


unsigned long socket_busy_stamp = 0;
int file_fragment = 0;
File dataFile;
int transfer_frags = 0;
char uart_rcv_package[1024];
uint uart_rcv_index = 0;
boolean printFinishFlag = false;
boolean transfer_file_flag = false;
boolean rcv_end_flag = false;
uint8_t dbgStr[100] ;


uint8_t blockBuf[FILE_BLOCK_SIZE] = {0};

//Struct
struct QUEUE {
    char buf[QUEUE_MAX_NUM][100];
    int rd_index;
    int wt_index;
} ;

struct QUEUE cmd_queue;



TRANS_STATE transfer_state = TRANSFER_IDLE;

OperatingState currentState = OperatingState::Unknown;

//Functions declaration
void esp_data_parser(char *cmdRxBuf, int len);
void handleGcode();
void StartAccessPoint();
bool TryToConnect();


//Functions definition
void init_queue(struct QUEUE *h_queue)
{
    if(h_queue == 0) {
        return;
    }

    h_queue->rd_index = 0;
    h_queue->wt_index = 0;
    memset(h_queue->buf, 0, sizeof(h_queue->buf));
}

int push_queue(struct QUEUE *h_queue, char *data_to_push, uint data_len)
{
    if(h_queue == 0) {
        return -1;
    }

    if(data_len > sizeof(h_queue->buf[h_queue->wt_index])) {
        return -1;
    }

    if((h_queue->wt_index + 1) % QUEUE_MAX_NUM == h_queue->rd_index) {
        return -1;
    }

    memset(h_queue->buf[h_queue->wt_index], 0, sizeof(h_queue->buf[h_queue->wt_index]));
    memcpy(h_queue->buf[h_queue->wt_index], data_to_push, data_len);

    h_queue->wt_index = (h_queue->wt_index + 1) % QUEUE_MAX_NUM;

    return 0;
}

int pop_queue(struct QUEUE *h_queue, char *data_for_pop, uint data_len)
{
    if(h_queue == 0) {
        return -1;
    }

    if(data_len < strlen(h_queue->buf[h_queue->rd_index])) {
        return -1;
    }

    if(h_queue->rd_index == h_queue->wt_index) {
        return -1;
    }

    memset(data_for_pop, 0, data_len);
    memcpy(data_for_pop, h_queue->buf[h_queue->rd_index], strlen(h_queue->buf[h_queue->rd_index]));

    h_queue->rd_index = (h_queue->rd_index + 1) % QUEUE_MAX_NUM;

    return 0;
}

void StartAccessPoint()
{
    delay(5000);
    IPAddress apIP(192, 168, 4, 1);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    String macStr= WiFi.macAddress();
    macStr.replace(":", "");
    strcat(softApName, macStr.substring(8).c_str());
    WiFi.softAP(softApName);
}

void verification()
{
    verification_flag = true;
}

// Try to connect using the saved SSID and password, returning true if successful
bool TryToConnect()
{

    char eeprom_valid[1] = {0};
    uint8_t failcount = 0;
    EEPROM.get(BAK_ADDRESS_WIFI_VALID, eeprom_valid);
    if(eeprom_valid[0] == 0x0a) {
        log_mkswifi("EEPROM is valid");
        log_mkswifi("Read SSID/Password from EEPROM");
        EEPROM.get(BAK_ADDRESS_WIFI_MODE, wifi_mode);
        EEPROM.get(BAK_ADDRESS_WEB_HOST, webhostname);
        log_mkswifi("Mode:%s, web hostname:%s", wifi_mode,webhostname);
    } else {
        log_mkswifi("EEPROM is not valid, reset it");
        memset(wifi_mode, 0, sizeof(wifi_mode));
        strcpy(wifi_mode, "wifi_mode_ap");
        log_mkswifi("Mode:%s", wifi_mode);
    }
    if(strcmp(wifi_mode, "wifi_mode_ap") != 0) {
        log_mkswifi("mode is NOT ap");
        if(eeprom_valid[0] == 0x0a) {
            log_mkswifi("EEPROM is valid");
            log_mkswifi("Read SSID/Password from EEPROM");
            EEPROM.get(BAK_ADDRESS_WIFI_SSID, ssid);
            EEPROM.get(BAK_ADDRESS_WIFI_KEY, pass);
            log_mkswifi("SSID:%s, pass:%s", ssid, pass);
        } else {
            log_mkswifi("EEPROM is not valid, reset it");
            memset(ssid, 0, sizeof(ssid));
            strcpy(ssid, "mks1");
            memset(pass, 0, sizeof(pass));
            strcpy(pass, "makerbase");
            log_mkswifi("SSID:%s, pass:%s", ssid,pass);
        }
        currentState = OperatingState::Client;
        log_mkswifi("Current state is client :%d", currentState);
        serialcom.sendNetworkInfos();
        log_mkswifi("Transfert state is: Ready");
        transfer_state = TRANSFER_READY;
        log_mkswifi("Wait 10s");
        delay(1000);
        log_mkswifi("Setup WiFi as STA");
        WiFi.mode(WIFI_STA);
        log_mkswifi("Disconnect from any AP");
        WiFi.disconnect();
        delay(1000);
        log_mkswifi("Check if static IP");
        EEPROM.get(BAK_ADDRESS_MANUAL_IP_FLAG, manual_valid);
        if(manual_valid == 0xa) {
            //uint32_t manual_ip, manual_subnet, manual_gateway, manual_dns;
            EEPROM.get(BAK_ADDRESS_MANUAL_IP, ip_static);
            EEPROM.get(BAK_ADDRESS_MANUAL_MASK, subnet_static);
            EEPROM.get(BAK_ADDRESS_MANUAL_GATEWAY, gateway_static);
            EEPROM.get(BAK_ADDRESS_MANUAL_DNS, dns_static);
            log_mkswifi("Use Static IP");
            WiFi.config(ip_static, gateway_static, subnet_static, dns_static, (uint32_t)0x00000000);
        }
        log_mkswifi("Setup WiFi as STA, SSID:%s, PWD:%s", ssid, pass);
        WiFi.begin(ssid, pass);
        log_mkswifi("Connecting");
        while (WiFi.status() != WL_CONNECTED) {
            if(get_printer_reply() > 0) {
                log_mkswifi("Read incoming data");
                esp_data_parser((char *)uart_rcv_package, uart_rcv_index);
            }
            uart_rcv_index = 0;
            serialcom.sendNetworkInfos();
            delay(500);
            failcount++;
            if (failcount > MAX_WIFI_FAIL) { // 1 min
                delay(100);
                log_mkswifi("Timeout");
                return false;
            }
            log_mkswifi("Do transfer");
            do_transfer();
        };
    } else {
        log_mkswifi("mode is ap");
        IPAddress apIP(192, 168, 4, 1);
        if(eeprom_valid[0] == 0x0a) {
            log_mkswifi("EEPROM is valid");
            log_mkswifi("Read SSID/Password from EEPROM");
            EEPROM.get(BAK_ADDRESS_WIFI_SSID, softApName);
            EEPROM.get(BAK_ADDRESS_WIFI_KEY, softApKey);
            log_mkswifi("SSID:%s, pass:%s", softApName,softApKey);
        } else {
            log_mkswifi("EEPROM is not valid, reset it");
            String macStr= WiFi.macAddress();
            macStr.replace(":", "");
            strcat(softApName, macStr.substring(8).c_str());
            memset(pass, 0, sizeof(pass));
            log_mkswifi("SSID:%s, no password", softApName);
        }
        currentState = OperatingState::AccessPoint;
        log_mkswifi("Current state is Access point :%d", currentState);

        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        if(strlen(softApKey) != 0) {
            WiFi.softAP(softApName, softApKey);
        } else {
            WiFi.softAP(softApName);
        }
        log_mkswifi("Setup WiFi as AP, SSID:%s, PWD:%s", softApName, softApKey);
    }
    return true;
}

void var_init()
{
    gPrinterInf.curBedTemp = 0.0;
    gPrinterInf.curSprayerTemp[0] = 0.0;
    gPrinterInf.curSprayerTemp[1] = 0.0;
    gPrinterInf.desireSprayerTemp[0] = 0.0;
    gPrinterInf.desireSprayerTemp[1] = 0.0;
    gPrinterInf.print_state = PRINTER_NOT_CONNECT;
    strcpy( wifi_mode, "wifi_mode_sta");
    strcpy( softApName, "MKSWIFI");
    String macStr= WiFi.macAddress();
    strcat( softApName, &macStr[macStr.length()-5]);
    strcpy( moduleId, "12345");
}

void setup()
{
    var_init();
    serialcom.begin();
    EEPROM.begin(EEPROM_SIZE);
    LittleFS.begin();
    verification();
    log_mkswifi("Setup Pins");
    pinMode(McuTfrReadyPin, INPUT);
    pinMode(EspReqTransferPin, OUTPUT);
    digitalWrite(EspReqTransferPin, HIGH);
    bool success = TryToConnect();
    if (success) {
        log_mkswifi("Success");
    } else {
        log_mkswifi("Start Access point");
        StartAccessPoint();
        currentState = OperatingState::AccessPoint;
    }
    serialcom.sendNetworkInfos();
    log_mkswifi("Start servers");
    WebServer.begin();
    TcpServer.begin();
    NodeMonitor.begin();
    delay(500);
}

void query_printer_inf()
{
    static int last_query_temp_time = 0;

    if((!transfer_file_flag) &&  (transfer_state == TRANSFER_IDLE)) {

        if((gPrinterInf.print_state == PRINTER_PRINTING) || (gPrinterInf.print_state == PRINTER_PAUSE)) {
            if(millis() - last_query_temp_time > 5000) { //every 5 seconds
                if(GET_VERSION_OK) {
                    serialcom.gcodeFragment("M27\nM992\nM994\nM991\nM997\n", false);
                } else {
                    serialcom.gcodeFragment("M27\nM992\nM994\nM991\nM997\nM115\n", false);
                }

                /*transfer_state = TRANSFER_READY;
                digitalWrite(EspReqTransferPin, LOW);*/

                last_query_temp_time = millis();
            }
        } else {
            if(millis() - last_query_temp_time > 5000) { //every 5 seconds

                if(GET_VERSION_OK) {
                    serialcom.gcodeFragment("M991\nM27\nM997\n", false);
                } else {
                    serialcom.gcodeFragment("M991\nM27\nM997\nM115\n", false);
                }

                /*transfer_state = TRANSFER_READY;
                digitalWrite(EspReqTransferPin, LOW);*/

                last_query_temp_time = millis();
            }

        }
    }
    if((!transfer_file_flag) &&  (transfer_state == TRANSFER_IDLE)) {
        //beat package
        serialcom.NetworkInfosFragment();
    }
    if((manual_valid == 0xa) && (!transfer_file_flag) &&  (transfer_state == TRANSFER_IDLE)) {
        //beat package
        serialcom.staticIPInfosFragment();

    }


}

int get_printer_reply()
{
    size_t len = Serial.available();

    if(len > 0) {

        len = ((uart_rcv_index + len) < sizeof(uart_rcv_package)) ? len : (sizeof(uart_rcv_package) - uart_rcv_index);


        Serial.readBytes(&uart_rcv_package[uart_rcv_index], len);

        uart_rcv_index += len;

        if(uart_rcv_index >= sizeof(uart_rcv_package)) {
            return sizeof(uart_rcv_package);
        }



    }
    return uart_rcv_index;

}

void loop()
{
    if(currentState != OperatingState::Unknown) {
        WebServer.handle();
        TcpServer.handle();

        do_transfer();
        if(get_printer_reply() > 0) {
            esp_data_parser((char *)uart_rcv_package, uart_rcv_index);
        }

        uart_rcv_index = 0;

        if(verification_flag) {
            query_printer_inf();
            //TODO move timeout to handle but need to check why socket_busy_stamp is reset else where
            if(millis() - socket_busy_stamp > 5000) {
                NodeMonitor.handle();
            }
        } else {
            verification();
        }
    }
    yield();

}

void do_transfer()
{
    static size_t readBytes;

    switch(transfer_state) {
    case TRANSFER_IDLE:
        if((uart_send_length_important > 0) || (uart_send_size > 0)) {
            digitalWrite(EspReqTransferPin, LOW);
            if(digitalRead(McuTfrReadyPin) == LOW) { // STM32 READY SIGNAL
                transfer_state = TRANSFER_FRAGMENT;
            } else {
                transfer_state = TRANSFER_READY;
            }
        }

        break;

    case TRANSFER_GET_FILE:
        serialcom.transferMode();
        readBytes = gFileFifo.pop((char *)blockBuf, FILE_BLOCK_SIZE);
        if(readBytes > 0) {
            if(rcv_end_flag && (readBytes < FILE_BLOCK_SIZE)) {
                file_fragment |= (1 << 31); //the last fragment
            } else {
                file_fragment &= ~(1 << 31);
            }

            serialcom.fileFragment(blockBuf, readBytes, file_fragment);

            digitalWrite(EspReqTransferPin, LOW);

            transfer_state = TRANSFER_READY;

            file_fragment++;


        } else if(rcv_end_flag) {
            memset(blockBuf, 0, sizeof(blockBuf));
            readBytes = 0;
            file_fragment |= (1 << 31); //the last fragment

            serialcom.fileFragment(blockBuf, readBytes, file_fragment);

            digitalWrite(EspReqTransferPin, LOW);

            transfer_state = TRANSFER_READY;
        }



        break;

    case TRANSFER_READY:

        if(digitalRead(McuTfrReadyPin) == LOW) { // STM32 READY SIGNAL
            transfer_state = TRANSFER_FRAGMENT;
        }

        break;

    case TRANSFER_FRAGMENT:

        if(uart_send_length_important > 0) {
            uart_send_length_important = (uart_send_length_important >= sizeof(uart_send_package_important) ? sizeof(uart_send_package_important) : uart_send_length_important);
            Serial.write(uart_send_package_important, uart_send_length_important);
            uart_send_length_important = 0;
            memset(uart_send_package_important, 0, sizeof(uart_send_package_important));
        } else {
            Serial.write(uart_send_package, uart_send_size);
            uart_send_size = 0;
            memset(uart_send_package, 0, sizeof(uart_send_package));
        }



        digitalWrite(EspReqTransferPin, HIGH);

        if(!transfer_file_flag) {
            transfer_state = TRANSFER_IDLE;
        } else {
            if(rcv_end_flag && (readBytes < FILE_BLOCK_SIZE)) {

                serialcom.communicationMode();
                transfer_file_flag = false;
                rcv_end_flag = false;
                transfer_state = TRANSFER_IDLE;


            } else {
                transfer_state = TRANSFER_GET_FILE;
            }
        }

        break;

    default:
        break;



    }
    if(transfer_file_flag) {
        if((gFileFifo.left() >= TCP_FRAG_LEN) && (transfer_frags >= TCP_FRAG_LEN)) {
            TcpServer.write((const uint8_t *) "ok\n", strlen((const char *)"ok\n"));
            transfer_frags -= TCP_FRAG_LEN;
        }
    }
}


/*******************************************************************
    receive data from stm32 handler

********************************************************************/
#define UART_RX_BUFFER_SIZE    1024

#define ESP_PROTOC_HEAD     (uint8_t)0xa5
#define ESP_PROTOC_TAIL     (uint8_t)0xfc

#define ESP_TYPE_NET            (uint8_t)0x0
#define ESP_TYPE_PRINTER        (uint8_t)0x1
#define ESP_TYPE_TRANSFER       (uint8_t)0x2
#define ESP_TYPE_EXCEPTION      (uint8_t)0x3
#define ESP_TYPE_CLOUD          (uint8_t)0x4
#define ESP_TYPE_UNBIND         (uint8_t)0x5
#define ESP_TYPE_WID            (uint8_t)0x6
#define ESP_TYPE_SCAN_WIFI      (uint8_t)0x7
#define ESP_TYPE_MANUAL_IP      (uint8_t)0x8
#define ESP_TYPE_WIFI_CTRL      (uint8_t)0x9


uint8_t esp_msg_buf[UART_RX_BUFFER_SIZE] = {0}; //麓忙麓垄麓媒麓娄脌铆碌脛脢媒戮脻
uint16_t esp_msg_index = 0; //脨麓脰赂脮毛

typedef struct {
    uint8_t head; //0xa5
    uint8_t type; //0x0:脡猫脰脙脥酶脗莽虏脦脢媒,0x1:麓貌脫隆禄煤脨脜脧垄,0x2:脥赂麓芦脨脜脧垄,0x3:脪矛鲁拢脨脜脧垄
    uint16_t dataLen; //脢媒戮脻鲁陇露脠
    uint8_t *data; //脫脨脨搂脢媒戮脻
    uint8_t tail; // 0xfc
} ESP_PROTOC_FRAME;


/*路碌禄脴脢媒脳茅脰脨脛鲁脳脰路没鲁枚脧脰脳卯脭莽碌脛脣梅脪媒潞脜拢卢麓脫0驴陋脢录,脠么虏禄麓忙脭脷脭貌路碌禄脴-1*/
static int32_t charAtArray(const uint8_t *_array, uint32_t _arrayLen, uint8_t _char)
{
    uint32_t i;
    for(i = 0; i < _arrayLen; i++) {
        if(*(_array + i) == _char) {
            return i;
        }
    }

    return -1;
}

static int cut_msg_head(uint8_t *msg, uint16_t msgLen, uint16_t cutLen)
{
    int i;

    if(msgLen < cutLen) {
        return 0;
    } else if(msgLen == cutLen) {
        memset(msg, 0, msgLen);
        return 0;
    }
    for(i = 0; i < (msgLen - cutLen); i++) {
        msg[i] = msg[cutLen + i];
    }
    memset(&msg[msgLen - cutLen], 0, cutLen);

    return msgLen - cutLen;

}

static void net_msg_handle(uint8_t * msg, uint16_t msgLen)
{
    uint8_t cfg_mode;
    uint8_t cfg_wifi_len;
    uint8_t *cfg_wifi;
    uint8_t cfg_key_len;
    uint8_t *cfg_key;

    char valid[1] = {0x0a};

    if(msgLen <= 0) {
        return;
    }

    //0x01:AP
    //0x02:Client
    //0x03:AP+Client(should not happen)
    if((msg[0] != 0x01) && (msg[0] != 0x02)) {
        return;
    }
    cfg_mode = msg[0];

    if(msg[1] > 32) {
        return;
    }
    cfg_wifi_len = msg[1];
    cfg_wifi = &msg[2];

    if(msg[2 +cfg_wifi_len ] > 64) {
        return;
    }
    cfg_key_len = msg[2 +cfg_wifi_len];
    cfg_key = &msg[3 +cfg_wifi_len];



    if((cfg_mode == 0x01) && ((currentState == OperatingState::Client)
                              || (cfg_wifi_len != strlen((const char *)softApName))
                              || (strncmp((const char *)cfg_wifi, (const char *)softApName, cfg_wifi_len) != 0)
                              || (cfg_key_len != strlen((const char *)softApKey))
                              || (strncmp((const char *)cfg_key,  (const char *)softApKey, cfg_key_len) != 0))) {
        if((cfg_key_len > 0) && (cfg_key_len < 8)) {
            return;
        }

        memset(softApName, 0, sizeof(softApName));
        memset(softApKey, 0, sizeof(softApKey));
        memset(wifi_mode, 0, sizeof(wifi_mode));

        strncpy((char *)softApName, (const char *)cfg_wifi, cfg_wifi_len);
        strncpy((char *)softApKey, (const char *)cfg_key, cfg_key_len);


        strcpy((char *)wifi_mode, "wifi_mode_ap");


        EEPROM.put(BAK_ADDRESS_WIFI_SSID, softApName);
        EEPROM.put(BAK_ADDRESS_WIFI_KEY, softApKey);

        EEPROM.put(BAK_ADDRESS_WIFI_MODE, wifi_mode);

        EEPROM.put(BAK_ADDRESS_WIFI_VALID, valid);

        EEPROM.commit();
        delay(300);
        ESP.restart();
    } else if((cfg_mode == 0x02) && ((currentState == OperatingState::AccessPoint)
                                     || (cfg_wifi_len != strlen((const char *)ssid))
                                     || (strncmp((const char *)cfg_wifi, (const char *)ssid, cfg_wifi_len) != 0)
                                     || (cfg_key_len != strlen((const char *)pass))
                                     || (strncmp((const char *)cfg_key,  (const char *)pass, cfg_key_len) != 0))) {
        memset(ssid, 0, sizeof(ssid));
        memset(pass, 0, sizeof(pass));
        memset(wifi_mode, 0, sizeof(wifi_mode));
        strncpy((char *)ssid, (const char *)cfg_wifi, cfg_wifi_len);
        strncpy((char *)pass, (const char *)cfg_key, cfg_key_len);

        strcpy((char *)wifi_mode, "wifi_mode_sta");

        EEPROM.put(BAK_ADDRESS_WIFI_SSID, ssid);
        EEPROM.put(BAK_ADDRESS_WIFI_KEY, pass);

        EEPROM.put(BAK_ADDRESS_WIFI_MODE, wifi_mode);

        EEPROM.put(BAK_ADDRESS_WIFI_VALID, valid);

        //Disable manual ip mode
        EEPROM.put(BAK_ADDRESS_MANUAL_IP_FLAG, 0xff);
        manual_valid = 0xff;

        EEPROM.commit();

        delay(300);
        ESP.restart();
    }

}

static void manual_ip_msg_handle(uint8_t * msg, uint16_t msgLen)
{

    if(msgLen < 16) {
        return;
    }

    ip_static = (msg[3] << 24) + (msg[2] << 16) + (msg[1] << 8) + msg[0];
    subnet_static = (msg[7] << 24) + (msg[6] << 16) + (msg[5] << 8) + msg[4];
    gateway_static = (msg[11] << 24) + (msg[10] << 16) + (msg[9] << 8) + msg[8];
    dns_static = (msg[15] << 24) + (msg[14] << 16) + (msg[13] << 8) + msg[12];

    manual_valid = 0xa;

    WiFi.config(ip_static, gateway_static, subnet_static, dns_static, (uint32_t)0x00000000);

    EEPROM.put(BAK_ADDRESS_MANUAL_IP, ip_static);
    EEPROM.put(BAK_ADDRESS_MANUAL_MASK, subnet_static);
    EEPROM.put(BAK_ADDRESS_MANUAL_GATEWAY, gateway_static);
    EEPROM.put(BAK_ADDRESS_MANUAL_DNS, dns_static);
    EEPROM.put(BAK_ADDRESS_MANUAL_IP_FLAG, manual_valid);

    EEPROM.commit();



}

static void wifi_ctrl_msg_handle(uint8_t * msg, uint16_t msgLen)
{
    if(msgLen != 1) {
        return;
    }

    uint8_t ctrl_code = msg[0];

    /*connect the wifi network*/
    if(ctrl_code == 0x1) {
        if(!WiFi.isConnected()) {
            WiFi.begin(ssid, pass);
        }
    }
    /*disconnect the wifi network*/
    else if(ctrl_code == 0x2) {
        if(WiFi.isConnected()) {
            WiFi.disconnect();
        }
    }
    /*disconnect the wifi network and forget the password*/
    else if(ctrl_code == 0x3) {
        if(WiFi.isConnected()) {
            WiFi.disconnect();
        }
        memset(ssid, 0, sizeof(ssid));
        memset(pass, 0, sizeof(pass));


        EEPROM.put(BAK_ADDRESS_WIFI_SSID, ssid);
        EEPROM.put(BAK_ADDRESS_WIFI_KEY, pass);

        EEPROM.put(BAK_ADDRESS_WIFI_VALID, 0Xff);

        //Disable manual ip mode
        EEPROM.put(BAK_ADDRESS_MANUAL_IP_FLAG, 0xff);
        manual_valid = 0xff;

        EEPROM.commit();
    }
}

static void transfer_msg_handle(uint8_t * msg, uint16_t msgLen)
{
    int j = 0;
    char cmd_line[100] = {0};

    init_queue(&cmd_queue);
    cmd_index = 0;
    memset(cmd_fifo, 0, sizeof(cmd_fifo));

    while(j < msgLen) {
        if((msg[j] == '\r') || (msg[j] == '\n')) {
            if((cmd_index) > 1) {
                cmd_fifo[cmd_index] = '\n';
                cmd_index++;


                push_queue(&cmd_queue, cmd_fifo, cmd_index);
            }
            memset(cmd_fifo, 0, sizeof(cmd_fifo));
            cmd_index = 0;
            log_mkswifi("push: %s",cmd_fifo);
        } else if(msg[j] == '\0') {
            break;
        } else {
            if(cmd_index >= sizeof(cmd_fifo)) {
                memset(cmd_fifo, 0, sizeof(cmd_fifo));
                cmd_index = 0;
            }
            cmd_fifo[cmd_index] = msg[j];
            cmd_index++;
        }

        j++;

        do_transfer();
        yield();

    }
    while(pop_queue(&cmd_queue, cmd_line, sizeof(cmd_line)) >= 0) {
        if(monitor_rx_buf.length() + strlen(cmd_line) < 500) {
            monitor_rx_buf.concat((const char *)cmd_line);
        } else {
            log_mkswifi("rx overflow");
        }
        /*handle the cmd*/
        paser_cmd((uint8_t *)cmd_line);
        do_transfer();
        yield();

        if((cmd_line[0] == 'T') && (cmd_line[1] == ':')) {
            String tempVal((const char *)cmd_line);
            int index = tempVal.indexOf("B:", 0);
            if(index != -1) {
                memset(dbgStr, 0, sizeof(dbgStr));
                sprintf((char *)dbgStr, "T:%d /%d B:%d /%d T0:%d /%d T1:%d /%d @:0 B@:0\r\n",
                        (int)gPrinterInf.curSprayerTemp[0], (int)gPrinterInf.desireSprayerTemp[0], (int)gPrinterInf.curBedTemp, (int)gPrinterInf.desireBedTemp,
                        (int)gPrinterInf.curSprayerTemp[0], (int)gPrinterInf.desireSprayerTemp[0], (int)gPrinterInf.curSprayerTemp[1], (int)gPrinterInf.desireSprayerTemp[1]);
                TcpServer.write((const uint8_t*)dbgStr, strlen((const char *)dbgStr));

            }
            continue;
        } else if((cmd_line[0] == 'M') && (cmd_line[1] == '9') && (cmd_line[2] == '9')
                  && ((cmd_line[3] == '7') ||  (cmd_line[3] == '2') ||  (cmd_line[3] == '4'))) {
            continue;
        } else if((cmd_line[0] == 'M') && (cmd_line[1] == '2') && (cmd_line[2] == '7')) {
            continue;
        } else {
            TcpServer.write((const uint8_t*)cmd_line, strlen((const char *)cmd_line));
        }


    }



}

void esp_data_parser(char *cmdRxBuf, int len)
{
    int32_t head_pos;
    int32_t tail_pos;
    uint16_t cpyLen;
    uint16_t leftLen = len;
    uint8_t loop_again = 0;

    ESP_PROTOC_FRAME esp_frame;


    while((leftLen > 0) || (loop_again == 1)) {
        loop_again = 0;

        /* 1. 虏茅脮脪脰隆脥路*/
        if(esp_msg_index != 0) {
            head_pos = 0;
            cpyLen = (leftLen < (sizeof(esp_msg_buf) - esp_msg_index)) ? leftLen : sizeof(esp_msg_buf) - esp_msg_index;

            memcpy(&esp_msg_buf[esp_msg_index], cmdRxBuf + len - leftLen, cpyLen);

            esp_msg_index += cpyLen;

            leftLen = leftLen - cpyLen;
            tail_pos = charAtArray(esp_msg_buf, esp_msg_index, ESP_PROTOC_TAIL);

            if(tail_pos == -1) {
                //脙禄脫脨脰隆脦虏
                if(esp_msg_index >= sizeof(esp_msg_buf)) {
                    memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
                    esp_msg_index = 0;
                }

                return;
            }
        } else {
            head_pos = charAtArray((uint8_t const *)&cmdRxBuf[len - leftLen], leftLen, ESP_PROTOC_HEAD);
            log_mkswifi("esp_data_parser1");
            if(head_pos == -1) {
                //脙禄脫脨脰隆脥路
                return;
            } else {
                //脧脠禄潞麓忙碌陆buf
                memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
                memcpy(esp_msg_buf, &cmdRxBuf[len - leftLen + head_pos], leftLen - head_pos);

                esp_msg_index = leftLen - head_pos;


                leftLen = 0;

                head_pos = 0;

                tail_pos = charAtArray(esp_msg_buf, esp_msg_index, ESP_PROTOC_TAIL);
                log_mkswifi("esp_data_parser2");
                if(tail_pos == -1) {
                    //脮脪碌陆脰隆脥路拢卢脙禄脫脨脰隆脦虏
                    return;
                }

            }
        }
        log_mkswifi("esp_data_parser3");
        /*3. 脮脪碌陆脥锚脮没碌脛脪禄脰隆 , 脜脨露脧脢媒戮脻鲁陇露脠*/
        esp_frame.type = esp_msg_buf[1];

        if((esp_frame.type != ESP_TYPE_NET) && (esp_frame.type != ESP_TYPE_PRINTER)
                && (esp_frame.type != ESP_TYPE_CLOUD) && (esp_frame.type != ESP_TYPE_UNBIND)
                && (esp_frame.type != ESP_TYPE_TRANSFER) && (esp_frame.type != ESP_TYPE_EXCEPTION)
                && (esp_frame.type != ESP_TYPE_WID) && (esp_frame.type != ESP_TYPE_SCAN_WIFI)
                && (esp_frame.type != ESP_TYPE_MANUAL_IP) && (esp_frame.type != ESP_TYPE_WIFI_CTRL)) {
            //脢媒戮脻脌脿脨脥虏禄脮媒脠路拢卢露陋脝煤
            memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
            esp_msg_index = 0;
            log_mkswifi("type error");
            return;
        }
        log_mkswifi("esp_data_parser4");
        esp_frame.dataLen = esp_msg_buf[2] + (esp_msg_buf[3] << 8);

        /*脢媒戮脻鲁陇露脠虏禄脮媒脠路*/
        if((uint16_t)(esp_frame.dataLen +4) > sizeof(esp_msg_buf)) {
            //脢媒戮脻鲁陇露脠虏禄脮媒脠路拢卢露陋脝煤
            memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
            esp_msg_index = 0;
            log_mkswifi("len error");
            return;
        }

        if(esp_msg_buf[4 + esp_frame.dataLen] != ESP_PROTOC_TAIL) {
            //脰隆脦虏虏禄脮媒脠路拢卢露陋脝煤
            memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
            log_mkswifi("tail error");
            esp_msg_index = 0;
            return;
        }

        /*4. 掳麓脮脮脌脿脨脥路脰卤冒麓娄脌铆脢媒戮脻*/
        esp_frame.data = &esp_msg_buf[4];




        switch(esp_frame.type) {
        case ESP_TYPE_NET:
            log_mkswifi("ESP_TYPE_NET");
            net_msg_handle(esp_frame.data, esp_frame.dataLen);
            break;

        case ESP_TYPE_PRINTER:
            //TODO ?
            //gcode_msg_handle(esp_frame.data, esp_frame.dataLen);
            break;

        case ESP_TYPE_TRANSFER:
            log_mkswifi("ESP_TYPE_TRANSFER");
            if(verification_flag) {
                transfer_msg_handle(esp_frame.data, esp_frame.dataLen);
            }
            break;

        case ESP_TYPE_CLOUD:
            //TODO
            break;

        case ESP_TYPE_EXCEPTION:
            log_mkswifi("ESP_TYPE_EXCEPTION");
            if(esp_frame.data[0] == 0x1) { // transfer error
                upload_error = true;
                log_mkswifi("Upload error");
            } else if(esp_frame.data[0] == 0x2) { // transfer sucessfully
                upload_success = true;
                log_mkswifi("Upload success");
            }
            break;

        case ESP_TYPE_UNBIND:
            if(cloud_link_state == 3) {
                unbind_exec = 1;
            }
            break;
        case ESP_TYPE_WID:
            //TODO
            break;

        case ESP_TYPE_SCAN_WIFI:
            log_mkswifi("ESP_TYPE_SCAN_WIFI");
            serialcom.hotSpotFragment();
            break;

        case ESP_TYPE_MANUAL_IP:
            log_mkswifi("ESP_TYPE_MANUAL_IP");
            manual_ip_msg_handle(esp_frame.data, esp_frame.dataLen);
            break;

        case ESP_TYPE_WIFI_CTRL:
            log_mkswifi("ESP_TYPE_WIFI_CTRL");
            wifi_ctrl_msg_handle(esp_frame.data, esp_frame.dataLen);
            break;

        default:
            log_mkswifi("Unknow type");
            break;
        }
        /*5. 掳脩脪脩麓娄脌铆碌脛脢媒戮脻陆脴碌么*/
        esp_msg_index = cut_msg_head(esp_msg_buf, esp_msg_index, esp_frame.dataLen  + 5);
        if(esp_msg_index > 0) {
            if(charAtArray(esp_msg_buf, esp_msg_index,  ESP_PROTOC_HEAD) == -1) {
                memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
                esp_msg_index = 0;
                return;
            }

            if((charAtArray(esp_msg_buf, esp_msg_index,  ESP_PROTOC_HEAD) != -1) && (charAtArray(esp_msg_buf, esp_msg_index, ESP_PROTOC_TAIL) != -1)) {
                loop_again = 1;
            }
        }
        yield();

    }
}

