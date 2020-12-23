#include <ESP8266WiFi.h>
#include "RepRapWebServer.h"
#include "MksHTTPUpdateServer.h"
#include <EEPROM.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESP8266HTTPClient.h>
#include "PooledStrings.cpp"
#include <WiFiUdp.h>
#include "Config.h"
#include "gcode.h"


//define
#define MAX_WIFI_FAIL 50
#define MAX_SRV_CLIENTS     1
#define QUEUE_MAX_NUM    10

#define BAK_ADDRESS_WIFI_SSID           0
#define BAK_ADDRESS_WIFI_KEY            (BAK_ADDRESS_WIFI_SSID + 32)
#define BAK_ADDRESS_WEB_HOST            (BAK_ADDRESS_WIFI_KEY+64)
#define BAK_ADDRESS_WIFI_MODE       (BAK_ADDRESS_WEB_HOST+64)
#define BAK_ADDRESS_WIFI_VALID      (BAK_ADDRESS_WIFI_MODE + 16)
#define BAK_ADDRESS_MODULE_ID       (BAK_ADDRESS_WIFI_VALID + 16)
#define BAK_ADDRESS_RESERVE1        (BAK_ADDRESS_MODULE_ID + 32)
#define BAK_ADDRESS_RESERVE2        (BAK_ADDRESS_RESERVE1 + 16)
#define BAK_ADDRESS_RESERVE3        (BAK_ADDRESS_RESERVE2 + 96)
#define BAK_ADDRESS_RESERVE4        (BAK_ADDRESS_RESERVE3 + 16)
#define BAK_ADDRESS_MANUAL_IP_FLAG  (BAK_ADDRESS_RESERVE4 + 1)
#define BAK_ADDRESS_MANUAL_IP           (BAK_ADDRESS_MANUAL_IP_FLAG + 1)
#define BAK_ADDRESS_MANUAL_MASK     (BAK_ADDRESS_MANUAL_IP + 4)
#define BAK_ADDRESS_MANUAL_GATEWAY  (BAK_ADDRESS_MANUAL_MASK + 4)
#define BAK_ADDRESS_MANUAL_DNS      (BAK_ADDRESS_MANUAL_GATEWAY + 4)

#define LIST_MIN_LEN_SAVE_FILE  100
#define LIST_MAX_LEN_SAVE_FILE  (1024 * 100)

#define CLOUD_HOST "baizhongyun.cn"
#define UDP_PORT    8989
#define TCP_FRAG_LEN    1400

#define FILE_FIFO_SIZE  (4096)
#define BUF_INC_POINTER(p)  ((p + 1 == FILE_FIFO_SIZE) ? 0:(p + 1))

#define FILE_BLOCK_SIZE (1024 - 5 - 4)


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

//Const

const char  firmwareVersion[] = "C1.0.4_201109_beta";


//Variable
char M3_TYPE = TFT28;
boolean GET_VERSION_OK = false;
char wifi_mode[14] = {0};
char moduleId[21] = {0};
char  softApName[96]={0};
char softApKey[64] = {0};
char ssid[32] = {0};
char pass[64] = {0};
char webhostname[64];
uint8_t manual_valid = 0xff; //whether it use static ip
uint32_t ip_static, subnet_static, gateway_staic, dns_static;

MksHTTPUpdateServer httpUpdater;
int cloud_port = 12345;
boolean cloud_enable_flag = false;
int cloud_link_state = 0;
RepRapWebServer server(80);
WiFiServer tcp(8080);
WiFiClient cloud_client;
String wifiConfigHtml;
volatile bool verification_flag = false;
IPAddress apIP(192, 168, 4, 1);
char filePath[100];
char cmd_fifo[100] = {0};
int cmd_index = 0;
WiFiUDP node_monitor;
WiFiClient serverClients[MAX_SRV_CLIENTS];

String monitor_tx_buf = "";
String monitor_rx_buf = "";

char uart_send_package[1024];
uint32_t uart_send_size;

char uart_send_package_important[1024]; //for the message that cannot missed
uint32_t uart_send_length_important;

char jsBuffer[1024];
char cloud_file_id[40];
char cloud_user_id[40];
char cloud_file_url[96];
char unbind_exec = 0;
bool upload_error = false;
bool upload_success = false;
uint32_t lastBeatTick = 0;
uint32_t lastStaticIpInfTick = 0;
unsigned long socket_busy_stamp = 0;
int file_fragment = 0;
File dataFile;
int transfer_frags = 0;
char uart_rcv_package[1024];
int uart_rcv_index = 0;
boolean printFinishFlag = false;
boolean transfer_file_flag = false;
boolean rcv_end_flag = false;
uint8_t dbgStr[100] ;
int NET_INF_UPLOAD_CYCLE = 10000;

//Struct 
struct QUEUE
{
    char buf[QUEUE_MAX_NUM][100];
    int rd_index;
    int wt_index;
} ;

struct QUEUE cmd_queue;

//Enum
typedef enum
{
    TRANSFER_IDLE,
    TRANSFER_BEGIN, 
    TRANSFER_GET_FILE,
    TRANSFER_READY,
    TRANSFER_FRAGMENT
    
} TRANS_STATE;

TRANS_STATE transfer_state = TRANSFER_IDLE;

typedef enum
{
    CLOUD_NOT_CONNECT,
    CLOUD_IDLE,
    CLOUD_DOWNLOADING,
    CLOUD_DOWN_WAIT_M3,
    CLOUD_DOWNLOAD_FINISH,
    CLOUD_WAIT_PRINT,
    CLOUD_PRINTING,
    CLOUD_GET_FILE,
} CLOUD_STATE;

CLOUD_STATE cloud_state = CLOUD_NOT_CONNECT;

enum class OperatingState
{
    Unknown = 0,
    Client = 1,
    AccessPoint = 2    
};

OperatingState currentState = OperatingState::Unknown;

//Functions declaration
int package_file_first(char *fileName);
int package_file_fragment(uint8_t *dataField, uint32_t fragLen, int32_t fragment);
void esp_data_parser(char *cmdRxBuf, int len);
String fileUrlEncode(String str);
String fileUrlEncode(char *array);
void cloud_handler();
void cloud_get_file_list();

void fsHandler();
void handleGcode();
void handleRrUpload();
void  handleUpload();

void urldecode(String &input);
void urlencode(String &input);
void StartAccessPoint();
void SendInfoToSam();
bool TryToConnect();
void onWifiConfig();

void cloud_down_file(const char *url);

//Class
 class FILE_FIFO
{
  public:  
    int push(char *buf, int len)
    {
        int i = 0;
        while(i < len )
        {
            if(rP != BUF_INC_POINTER(wP))
            {
                fifo[wP] = *(buf + i) ;

                wP = BUF_INC_POINTER(wP);

                i++;
            }
            else
            {
                break;
            }
            
        }
        return i;
    }
    
    int pop(char * buf, int len)
    {   
        int i = 0;
        
        while(i < len)
        {
            if(rP != wP)
            {
                buf[i] = fifo[rP];
                rP= BUF_INC_POINTER(rP);
                i++;                
            }
            else
            {
                break;
            }
        }
        return i;
        
    }
    
    void reset()
    {       
        wP = 0; 
        rP = 0;
        memset(fifo, 0, FILE_FIFO_SIZE);
    }

    uint32_t left()
    {       
        if(rP >  wP)
            return rP - wP - 1;
        else
            return FILE_FIFO_SIZE + rP - wP - 1;
            
    }
    
    boolean is_empty()
    {
        if(rP == wP)
            return true;
        else
            return false;
    }

private:
    char fifo[FILE_FIFO_SIZE]; 
    uint32_t wP;    
    uint32_t rP;

};

class FILE_FIFO gFileFifo;

//Functions definition
void init_queue(struct QUEUE *h_queue)
{
    if(h_queue == 0)
        return;
    
    h_queue->rd_index = 0;
    h_queue->wt_index = 0;
    memset(h_queue->buf, 0, sizeof(h_queue->buf));
}

int push_queue(struct QUEUE *h_queue, char *data_to_push, int data_len)
{
    if(h_queue == 0)
        return -1;

    if(data_len > sizeof(h_queue->buf[h_queue->wt_index]))
        return -1;

    if((h_queue->wt_index + 1) % QUEUE_MAX_NUM == h_queue->rd_index)
        return -1;

    memset(h_queue->buf[h_queue->wt_index], 0, sizeof(h_queue->buf[h_queue->wt_index]));
    memcpy(h_queue->buf[h_queue->wt_index], data_to_push, data_len);

    h_queue->wt_index = (h_queue->wt_index + 1) % QUEUE_MAX_NUM;
    
    return 0;
}

int pop_queue(struct QUEUE *h_queue, char *data_for_pop, int data_len)
{
    if(h_queue == 0)
        return -1;

    if(data_len < strlen(h_queue->buf[h_queue->rd_index]))
        return -1;

    if(h_queue->rd_index == h_queue->wt_index)
        return -1;

    memset(data_for_pop, 0, data_len);
    memcpy(data_for_pop, h_queue->buf[h_queue->rd_index], strlen(h_queue->buf[h_queue->rd_index]));

    h_queue->rd_index = (h_queue->rd_index + 1) % QUEUE_MAX_NUM;
    
    return 0;
}
bool smartConfig()
{
    WiFi.mode(WIFI_STA);
    WiFi.beginSmartConfig();
    int now = millis();
    while (1)
    {
        if(get_printer_reply() > 0)
        {
            esp_data_parser((char *)uart_rcv_package, uart_rcv_index);
        }
        uart_rcv_index = 0;
        
        delay(1000);
        if (WiFi.smartConfigDone())
        {
            
            

            WiFi.stopSmartConfig();
            return true;;
        }

        if(millis() - now > 120000) // 2min
        {
            WiFi.stopSmartConfig();
            return false;
        }
    }
}

void net_env_prepare()
{

    if(verification_flag)
    {
        LittleFS.begin();
        server.onNotFound(fsHandler);
    }
    
    
    server.servePrinter(true);

    
    onWifiConfig();

    server.onPrefix("/upload", HTTP_ANY, handleUpload, handleRrUpload);     
    

    server.begin();
    tcp.begin();

    
    
    node_monitor.begin(UDP_PORT);

    
}

void reply_search_handler()
{
    char packetBuffer[200];
     int packetSize = node_monitor.parsePacket();
     char  ReplyBuffer[50] = "mkswifi:";
     
     
      if (packetSize)
      {
        // read the packet into packetBufffer
        node_monitor.read(packetBuffer, sizeof(packetBuffer));

        if(strstr(packetBuffer, "mkswifi"))
        {
            memcpy(&ReplyBuffer[strlen("mkswifi:")], moduleId, strlen(moduleId)); 
            ReplyBuffer[strlen("mkswifi:") + strlen(moduleId)] = ',';
            if(currentState == OperatingState::Client)
            {
                strcpy(&ReplyBuffer[strlen("mkswifi:") + strlen(moduleId) + 1], WiFi.localIP().toString().c_str()); 
                ReplyBuffer[strlen("mkswifi:") + strlen(moduleId) + strlen(WiFi.localIP().toString().c_str()) + 1] = '\n';
            } 
            else if(currentState == OperatingState::AccessPoint)
            {
                strcpy(&ReplyBuffer[strlen("mkswifi:") + strlen(moduleId) + 1], WiFi.softAPIP().toString().c_str()); 
                ReplyBuffer[strlen("mkswifi:") + strlen(moduleId) + strlen(WiFi.softAPIP().toString().c_str()) + 1] = '\n';
            }
            

        // send a reply, to the IP address and port that sent us the packet we received
            node_monitor.beginPacket(node_monitor.remoteIP(), node_monitor.remotePort());
            node_monitor.write(ReplyBuffer, strlen(ReplyBuffer));
            node_monitor.endPacket();
        }
      }
}

void verification()
{
    verification_flag = true;
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
    strcpy( moduleId, "12345");
}



void setup() {
    var_init();
    Serial.begin(115200);
    delay(20);
    EEPROM.begin(512);  
    verification();
    String macStr= WiFi.macAddress();
    log_esp3d("Setup Pins");
    pinMode(McuTfrReadyPin, INPUT);
    pinMode(EspReqTransferPin, OUTPUT);
    digitalWrite(EspReqTransferPin, HIGH);
    
    bool success = TryToConnect();
    if (success)
    {
        log_esp3d("Success");
    } else
    {
        log_esp3d("Start Access point");
        StartAccessPoint();     
        currentState = OperatingState::AccessPoint;
    }
    package_net_para();
    log_esp3d("Sending Net Frame");
    Serial.write(uart_send_package, uart_send_size);
    
    
    net_env_prepare();   
    
    httpUpdater.setup(&server);
    delay(500);
}

void net_print(const uint8_t *sbuf, uint32_t len)
{
    int i;
    
    for(i = 0; i < MAX_SRV_CLIENTS; i++){
            
      if (serverClients[i] && serverClients[i].connected()){
        serverClients[i].write(sbuf, len);
        delay(1);
        
      }
    }
}

void query_printer_inf()
{
    static int last_query_temp_time = 0;
    static int last_query_file_time = 0;

    if((!transfer_file_flag) &&  (transfer_state == TRANSFER_IDLE))
    {       
        
        if((gPrinterInf.print_state == PRINTER_PRINTING) || (gPrinterInf.print_state == PRINTER_PAUSE))
        {
            if(millis() - last_query_temp_time > 5000) //every 5 seconds
            {
                if(GET_VERSION_OK)
                    package_gcode("M27\nM992\nM994\nM991\nM997\n", false);
                else
                    package_gcode("M27\nM992\nM994\nM991\nM997\nM115\n", false);
                
                /*transfer_state = TRANSFER_READY;
                digitalWrite(EspReqTransferPin, LOW);*/

                last_query_temp_time = millis();
            }
        }
        else
        {
            if(millis() - last_query_temp_time > 5000) //every 5 seconds
            {
                
                if(GET_VERSION_OK)
                    //package_gcode("M27\nM997\n");
                    package_gcode("M991\nM27\nM997\n", false);
                else
                    //package_gcode("M27\nM997\nM115\n");
                    package_gcode("M991\nM27\nM997\nM115\n", false);
                
                /*transfer_state = TRANSFER_READY;
                digitalWrite(EspReqTransferPin, LOW);*/

                last_query_temp_time = millis();
            }
            
        }
    }
    if((!transfer_file_flag) &&  (transfer_state == TRANSFER_IDLE))
    {

        //beat package
        if(millis() - lastBeatTick > NET_INF_UPLOAD_CYCLE)
        {
            package_net_para();
            /*transfer_state = TRANSFER_READY;
            digitalWrite(EspReqTransferPin, LOW);*/
            lastBeatTick = millis();
        }
    
    }
    if((manual_valid == 0xa) && (!transfer_file_flag) &&  (transfer_state == TRANSFER_IDLE))
    {

        //beat package
        if(millis() - lastStaticIpInfTick > (NET_INF_UPLOAD_CYCLE + 2))
        {
            package_static_ip_info();
            /*transfer_state = TRANSFER_READY;
            digitalWrite(EspReqTransferPin, LOW);*/
            lastStaticIpInfTick = millis();
        }
    
    }

    
}

int get_printer_reply()
{
    size_t len = Serial.available();

    if(len > 0){
        
        len = ((uart_rcv_index + len) < sizeof(uart_rcv_package)) ? len : (sizeof(uart_rcv_package) - uart_rcv_index);

                
        Serial.readBytes(&uart_rcv_package[uart_rcv_index], len);

        uart_rcv_index += len;  

        if(uart_rcv_index >= sizeof(uart_rcv_package))
        {           
            return sizeof(uart_rcv_package);
        }
        

        
    }
    return uart_rcv_index;

}

void loop()
{
    int i;
    #if 1
    
    
    switch (currentState)
    {
        case OperatingState::Client:
            server.handleClient();
            if(verification_flag)
            {
                cloud_handler();
            }
            break;

        case OperatingState::AccessPoint:
            server.handleClient();
            break;

        default:
            break;
    }


    
    
    
    //  if(transfer_state == TRANSFER_IDLE)
        {
            if (tcp.hasClient()){
                for(i = 0; i < MAX_SRV_CLIENTS; i++){
                  //find free/disconnected spot
                  if(serverClients[i].connected()) 
                  {
                    serverClients[i].stop();
                  }
                  serverClients[i] = tcp.available();
                }
                if (tcp.hasClient())
                {
                    //no free/disconnected spot so reject
                    WiFiClient serverClient = tcp.available();
                    serverClient.stop();
                    
                }
            }
            memset(dbgStr, 0, sizeof(dbgStr));
            for(i = 0; i < MAX_SRV_CLIENTS; i++)
            {
                if (serverClients[i] && serverClients[i].connected())
                {
                    uint32_t readNum = serverClients[i].available();

                    if(readNum > FILE_FIFO_SIZE)
                    {
                        serverClients[i].flush(); 
                        continue;
                    }

                
                    if(readNum > 0)
                    {
                        char * point;
                        
                        uint8_t readStr[readNum + 1] ;

                        uint32_t readSize;
                        
                        readSize = serverClients[i].read(readStr, readNum);
                            
                        readStr[readSize] = 0;
                        
                        if(transfer_file_flag)
                        {
                        
                            if(!verification_flag)
                            {
                                break;
                            }
                            if(gFileFifo.left() >= readSize)
                            {
                            
                                gFileFifo.push((char *)readStr, readSize);
                                transfer_frags += readSize;
                                
                                
                            }
                        
                        }
                        else
                        {
                            

                            if(verification_flag)
                            {
                                int j = 0;
                                char cmd_line[100] = {0};
                                String gcodeM3 = "";
                                
                                init_queue(&cmd_queue);
                                
                                cmd_index = 0;
                                memset(cmd_fifo, 0, sizeof(cmd_fifo));
                                while(j < readSize)
                                {
                                    if((readStr[j] == '\r') || (readStr[j] == '\n'))
                                    {
                                        if((cmd_index) > 1)
                                        {
                                            cmd_fifo[cmd_index] = '\n';
                                            cmd_index++;

                                            
                                            push_queue(&cmd_queue, cmd_fifo, cmd_index);
                                        }
                                        memset(cmd_fifo, 0, sizeof(cmd_fifo));
                                        cmd_index = 0;
                                    }
                                    else if(readStr[j] == '\0')
                                        break;
                                    else
                                    {
                                        if(cmd_index >= sizeof(cmd_fifo))
                                        {
                                            memset(cmd_fifo, 0, sizeof(cmd_fifo));
                                            cmd_index = 0;
                                        }
                                        cmd_fifo[cmd_index] = readStr[j];
                                        cmd_index++;
                                    }

                                    j++;

                                    do_transfer();
                                    yield();
                                
                                }
                                while(pop_queue(&cmd_queue, cmd_line, sizeof(cmd_line)) >= 0)       
                                {
                                    {
                                        /*transfer gcode*/
                                        if((strchr((const char *)cmd_line, 'G') != 0) 
                                            || (strchr((const char *)cmd_line, 'M') != 0)
                                            || (strchr((const char *)cmd_line, 'T') != 0))
                                        {
                                            if(strchr((const char *)cmd_line, '\n') != 0 )
                                            {
                                                String gcode((const char *)cmd_line);

                                                if(gcode.startsWith("M998") && (M3_TYPE == ROBIN))
                                                {
                                                    net_print((const uint8_t *) "ok\r\n", strlen((const char *)"ok\r\n"));
                                                }
                                                else if(gcode.startsWith("M997"))
                                                {
                                                    if(gPrinterInf.print_state == PRINTER_IDLE)
                                                        strcpy((char *)dbgStr, "M997 IDLE\r\n");
                                                    else if(gPrinterInf.print_state == PRINTER_PRINTING)
                                                        strcpy((char *)dbgStr, "M997 PRINTING\r\n");
                                                    else if(gPrinterInf.print_state == PRINTER_PAUSE)
                                                        strcpy((char *)dbgStr, "M997 PAUSE\r\n");
                                                    else
                                                        strcpy((char *)dbgStr, "M997 NOT CONNECTED\r\n");
                                                }
                                                else if(gcode.startsWith("M27"))
                                                {
                                                    memset(dbgStr, 0, sizeof(dbgStr));
                                                    sprintf((char *)dbgStr, "M27 %d\r\n", gPrinterInf.print_file_inf.print_rate);
                                                }
                                                else if(gcode.startsWith("M992"))
                                                {
                                                    memset(dbgStr, 0, sizeof(dbgStr));
                                                    sprintf((char *)dbgStr, "M992 %02d:%02d:%02d\r\n", 
                                                        gPrinterInf.print_file_inf.print_hours, gPrinterInf.print_file_inf.print_mins, gPrinterInf.print_file_inf.print_seconds);
                                                }
                                                else if(gcode.startsWith("M994"))
                                                {
                                                    memset(dbgStr, 0, sizeof(dbgStr));
                                                    sprintf((char *)dbgStr, "M994 %s;%d\r\n", 
                                                        gPrinterInf.print_file_inf.file_name.c_str(), gPrinterInf.print_file_inf.file_size);                                                        
                                                }
                                                else  if(gcode.startsWith("M115"))
                                                {
                                                    memset(dbgStr, 0, sizeof(dbgStr));
                                                    if(M3_TYPE == ROBIN)
                                                        strcpy((char *)dbgStr, "FIRMWARE_NAME:Robin\r\n");
                                                    else if(M3_TYPE == TFT28)
                                                        strcpy((char *)dbgStr, "FIRMWARE_NAME:TFT28/32\r\n");
                                                    else if(M3_TYPE == TFT24)
                                                        strcpy((char *)dbgStr, "FIRMWARE_NAME:TFT24\r\n");
                                                    
                                                    
                                                }
                                                else
                                                {   
                                                    if(gPrinterInf.print_state == PRINTER_IDLE)
                                                    {
                                                        if(gcode.startsWith("M23") || gcode.startsWith("M24"))
                                                         {
                                                            gPrinterInf.print_state = PRINTER_PRINTING;
                                                            gPrinterInf.print_file_inf.file_name = "";
                                                            gPrinterInf.print_file_inf.file_size = 0;
                                                            gPrinterInf.print_file_inf.print_rate = 0;
                                                            gPrinterInf.print_file_inf.print_hours = 0;
                                                            gPrinterInf.print_file_inf.print_mins = 0;
                                                            gPrinterInf.print_file_inf.print_seconds = 0;

                                                            printFinishFlag = false;
                                                         }
                                                    }
                                                    gcodeM3.concat(gcode);
                                                    
                                                }
                                                
                                            }
                                        }
                                    }
                                    if(strlen((const char *)dbgStr) > 0)
                                    {
                                        net_print((const uint8_t *) "ok\r\n", strlen((const char *)"ok\r\n"));
                                    
                                        net_print((const uint8_t *) dbgStr, strlen((const char *)dbgStr));      
                                        memset(dbgStr, 0, sizeof(dbgStr));          
                                        
                                    }
                                        

                                
                                    do_transfer();
                                    yield();
                                
                                    
                                    
                                }
                                
                                if(gcodeM3.length() > 2)
                                {
                                    package_gcode(gcodeM3, true);
                                    //Serial.write(uart_send_package, sizeof(uart_send_package));
                                    /*transfer_state = TRANSFER_READY;
                                    digitalWrite(EspReqTransferPin, LOW);*/
                                    do_transfer();

                                    socket_busy_stamp = millis();
                                }
                                
                                
                            }
                        }
                    
                    }
                }
            }
        /*  if(strlen((const char *)dbgStr) > 0)
            {
                net_print((const uint8_t *) dbgStr, strlen((const char *)dbgStr));
                net_print((const uint8_t *) "ok\r\n", strlen((const char *)"ok\r\n"));
            }*/
        }
        //sprintf((char *)dbgStr, "state:%d\n", transfer_state);
        //net_print((const uint8_t *)dbgStr);


            
            do_transfer();
        
            

            if(get_printer_reply() > 0)
            {
                esp_data_parser((char *)uart_rcv_package, uart_rcv_index);
            }

            uart_rcv_index = 0;

        if(verification_flag)
        {
            query_printer_inf();    
            if(millis() - socket_busy_stamp > 5000)
            {               
                reply_search_handler();
            }
            cloud_down_file();
            cloud_get_file_list();
        }
        else
        {
            verification();
        }
    
    yield();
    #endif

}



int package_net_para()
{
    int dataLen;
    int wifi_name_len;
    int wifi_key_len;
    int host_len = strlen(CLOUD_HOST);
    log_esp3d("Net Frame preparation");
    log_esp3d("Clear buffer");
    memset(uart_send_package, 0, sizeof(uart_send_package));
    log_esp3d("Set frame header");
    uart_send_package[UART_PROTCL_HEAD_OFFSET] = UART_PROTCL_HEAD;
    uart_send_package[UART_PROTCL_TYPE_OFFSET] = UART_PROTCL_TYPE_NET;

    if(currentState == OperatingState::Client)
    {
        log_esp3d("STA Mode");
        if(WiFi.status() == WL_CONNECTED)
        {
            log_esp3d("Connected : %s", WiFi.localIP().toString().c_str());
            uart_send_package[UART_PROTCL_DATA_OFFSET] = WiFi.localIP()[0];
            uart_send_package[UART_PROTCL_DATA_OFFSET + 1] = WiFi.localIP()[1];
            uart_send_package[UART_PROTCL_DATA_OFFSET + 2] = WiFi.localIP()[2];
            uart_send_package[UART_PROTCL_DATA_OFFSET + 3] = WiFi.localIP()[3];
            uart_send_package[UART_PROTCL_DATA_OFFSET + 6] = 0x0a;
        }
        else
        {
            log_esp3d("Not Connected");
            uart_send_package[UART_PROTCL_DATA_OFFSET] = 0;
            uart_send_package[UART_PROTCL_DATA_OFFSET + 1] = 0;
            uart_send_package[UART_PROTCL_DATA_OFFSET + 2] = 0;
            uart_send_package[UART_PROTCL_DATA_OFFSET + 3] = 0;
            uart_send_package[UART_PROTCL_DATA_OFFSET + 6] = 0x05;
        }

        uart_send_package[UART_PROTCL_DATA_OFFSET + 7] = 0x02;

        wifi_name_len = strlen(ssid);
        wifi_key_len = strlen(pass);
        
        uart_send_package[UART_PROTCL_DATA_OFFSET + 8] = wifi_name_len;

        strcpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + 9], ssid);

        uart_send_package[UART_PROTCL_DATA_OFFSET + 9 + wifi_name_len] = wifi_key_len;

        strcpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + 10 + wifi_name_len], pass); 

        
    } 
    else if(currentState == OperatingState::AccessPoint)
    {
        log_esp3d("AP Mode: %s", WiFi.softAPIP().toString().c_str());
        uart_send_package[UART_PROTCL_DATA_OFFSET] = WiFi.softAPIP()[0];
        uart_send_package[UART_PROTCL_DATA_OFFSET + 1] = WiFi.softAPIP()[1];
        uart_send_package[UART_PROTCL_DATA_OFFSET + 2] = WiFi.softAPIP()[2];
        uart_send_package[UART_PROTCL_DATA_OFFSET + 3] = WiFi.softAPIP()[3];
        
        uart_send_package[UART_PROTCL_DATA_OFFSET + 6] = 0x0a;
        uart_send_package[UART_PROTCL_DATA_OFFSET + 7] = 0x01;

        
        wifi_name_len = strlen(softApName);
        wifi_key_len = strlen(softApKey);
        log_esp3d("SSID (%d): %s, PWD (%d):%s",wifi_name_len,softApName, wifi_key_len,softApKey);
        uart_send_package[UART_PROTCL_DATA_OFFSET + 8] = wifi_name_len;
        strcpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + 9], softApName);
        uart_send_package[UART_PROTCL_DATA_OFFSET + 9 + wifi_name_len] = wifi_key_len;
        strcpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + 10 + wifi_name_len], softApKey);    
    }

    
    
    if(cloud_enable_flag)
    {
        log_esp3d("Cloud service is enabled");
        if(cloud_link_state == 3)
            uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 10] =0x12;
        else if( (cloud_link_state == 1) || (cloud_link_state == 2))
            uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 10] =0x11;
        else if(cloud_link_state == 0)
            uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 10] =0x10;
    }
    else
    {
        log_esp3d("Cloud service is disabled");
        uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 10] =0x0;
    
    }
    
    log_esp3d("Cloud Host (%d): %s, port: %d", host_len, CLOUD_HOST, cloud_port);
    uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 11] = host_len;
    strcpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + 12], CLOUD_HOST);
    uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + 12] = cloud_port & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + 13] = (cloud_port >> 8 ) & 0xff;

    
    int id_len = strlen(moduleId);
    log_esp3d("ModuleID (%d): %s", id_len, moduleId);
    uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + 14]  = id_len;
    strcpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + 15], moduleId);
        
    int ver_len = strlen((const char *)firmwareVersion);
    log_esp3d("FW (%d): %s", ver_len, firmwareVersion);
    uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + id_len + 15]  = ver_len;
    strcpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + wifi_name_len + wifi_key_len + host_len + id_len + 16], firmwareVersion);
        
    dataLen = wifi_name_len + wifi_key_len + host_len + id_len + ver_len + 16;
    log_esp3d("Cloud service Port: %d", 8080);
    uart_send_package[UART_PROTCL_DATA_OFFSET + 4] = 8080 & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 5] = (8080 >> 8 )& 0xff;

    if(!verification_flag) {
            uart_send_package[UART_PROTCL_DATA_OFFSET + 6] = 0x0e;
             log_esp3d("Exception state");
        }

    log_esp3d("Data len: %d", dataLen);
    uart_send_package[UART_PROTCL_DATALEN_OFFSET] = dataLen & 0xff;
    uart_send_package[UART_PROTCL_DATALEN_OFFSET + 1] = (dataLen >> 8 )& 0xff;
    
    
    uart_send_package[dataLen + 4] = UART_PROTCL_TAIL;

    uart_send_size = dataLen + 5;
    
    return uart_send_size;
}

int package_static_ip_info()
{
    int dataLen;
    int wifi_name_len;
    int wifi_key_len;
    
    memset(uart_send_package, 0, sizeof(uart_send_package));
    uart_send_package[UART_PROTCL_HEAD_OFFSET] = UART_PROTCL_HEAD;
    uart_send_package[UART_PROTCL_TYPE_OFFSET] = UART_PROTCL_TYPE_STATIC_IP;

    uart_send_package[UART_PROTCL_DATA_OFFSET] = ip_static & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 1] = (ip_static >> 8) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 2] = (ip_static >> 16) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 3] = (ip_static >> 24) & 0xff;

    uart_send_package[UART_PROTCL_DATA_OFFSET + 4] = subnet_static & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 5] = (subnet_static >> 8) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 6] = (subnet_static >> 16) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 7] = (subnet_static >> 24) & 0xff;

    uart_send_package[UART_PROTCL_DATA_OFFSET + 8] = gateway_staic & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 9] = (gateway_staic >> 8) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 10] = (gateway_staic >> 16) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 11] = (gateway_staic >> 24) & 0xff;

    uart_send_package[UART_PROTCL_DATA_OFFSET + 12] = dns_static & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 13] = (dns_static >> 8) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 14] = (dns_static >> 16) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 15] = (dns_static >> 24) & 0xff;

    uart_send_package[UART_PROTCL_DATALEN_OFFSET] = 16;
    uart_send_package[UART_PROTCL_DATALEN_OFFSET + 1] = 0;  
    
    uart_send_package[UART_PROTCL_DATA_OFFSET + 16] = UART_PROTCL_TAIL;

    uart_send_size = UART_PROTCL_DATA_OFFSET + 17;
}

int package_gcode(String gcodeStr, boolean important)
{
    int dataLen;
    const char *dataField = gcodeStr.c_str();
    
    uint32_t buffer_offset;
    
    dataLen = strlen(dataField);
    
    if(dataLen > 1019)
        return -1;

    if(important)
    {   
        buffer_offset = uart_send_length_important;
    }
    else
    {       
        buffer_offset = 0;
        memset(uart_send_package, 0, sizeof(uart_send_package));
    }
    
    if(dataLen + buffer_offset > 1019)
        return -1;
    
    //net_print((const uint8_t *)"dataField:", strlen("dataField:"));
    //net_print((const uint8_t *)dataField, strlen(dataField));
    //net_print((const uint8_t *)"\n", 1);
    if(important)
    {
        uart_send_package_important[UART_PROTCL_HEAD_OFFSET + buffer_offset] = UART_PROTCL_HEAD;
        uart_send_package_important[UART_PROTCL_TYPE_OFFSET + buffer_offset] = UART_PROTCL_TYPE_GCODE;
        uart_send_package_important[UART_PROTCL_DATALEN_OFFSET + buffer_offset] = dataLen & 0xff;
        uart_send_package_important[UART_PROTCL_DATALEN_OFFSET + buffer_offset + 1] = dataLen >> 8;
        
        strncpy(&uart_send_package_important[UART_PROTCL_DATA_OFFSET + buffer_offset], dataField, dataLen);
        
        uart_send_package_important[dataLen + buffer_offset + 4] = UART_PROTCL_TAIL;

        uart_send_length_important += dataLen + 5;
    }
    else
    {   
        uart_send_package[UART_PROTCL_HEAD_OFFSET + buffer_offset] = UART_PROTCL_HEAD;
        uart_send_package[UART_PROTCL_TYPE_OFFSET + buffer_offset] = UART_PROTCL_TYPE_GCODE;
        uart_send_package[UART_PROTCL_DATALEN_OFFSET + buffer_offset] = dataLen & 0xff;
        uart_send_package[UART_PROTCL_DATALEN_OFFSET + buffer_offset + 1] = dataLen >> 8;
        
        strncpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + buffer_offset], dataField, dataLen);
        
        uart_send_package[dataLen + buffer_offset + 4] = UART_PROTCL_TAIL;

        uart_send_size = dataLen + 5;
    }

    

    if(monitor_tx_buf.length() + gcodeStr.length() < 300)
    {
        monitor_tx_buf.concat(gcodeStr);
    }
    else
    {
        //net_print((const uint8_t *)"overflow", strlen("overflow"));
    }
    

    return 0;
}

int package_gcode(char *dataField, boolean important)
{   
    uint32_t buffer_offset;
    int dataLen = strlen((const char *)dataField);

    if(important)
    {       
        
        buffer_offset = uart_send_length_important;
    }
    else
    {
        buffer_offset = 0;
        memset(uart_send_package, 0, sizeof(uart_send_package));
    }
    if(dataLen + buffer_offset > 1019)
        return -1;
    //net_print((const uint8_t *)"dataField:", strlen("dataField:"));
    //net_print((const uint8_t *)dataField, strlen(dataField));
    //net_print((const uint8_t *)"\n", 1);

    /**(buffer_to_send + UART_PROTCL_HEAD_OFFSET + buffer_offset) = UART_PROTCL_HEAD;
    *(buffer_to_send + UART_PROTCL_TYPE_OFFSET + buffer_offset) = UART_PROTCL_TYPE_GCODE;
    *(buffer_to_send + UART_PROTCL_DATALEN_OFFSET + buffer_offset) = dataLen & 0xff;
    *(buffer_to_send + UART_PROTCL_DATALEN_OFFSET + buffer_offset + 1) = dataLen >> 8;
    strncpy(buffer_to_send + UART_PROTCL_DATA_OFFSET + buffer_offset, dataField, dataLen);
    
    *(buffer_to_send + dataLen + buffer_offset + 4) = UART_PROTCL_TAIL;
*/
    
    if(important)
    {
        uart_send_package_important[UART_PROTCL_HEAD_OFFSET + buffer_offset] = UART_PROTCL_HEAD;
        uart_send_package_important[UART_PROTCL_TYPE_OFFSET + buffer_offset] = UART_PROTCL_TYPE_GCODE;
        uart_send_package_important[UART_PROTCL_DATALEN_OFFSET + buffer_offset] = dataLen & 0xff;
        uart_send_package_important[UART_PROTCL_DATALEN_OFFSET + buffer_offset + 1] = dataLen >> 8;
        
        strncpy(&uart_send_package_important[UART_PROTCL_DATA_OFFSET + buffer_offset], dataField, dataLen);
        
        uart_send_package_important[dataLen + buffer_offset + 4] = UART_PROTCL_TAIL;

        uart_send_length_important += dataLen + 5;
    }
    else
    {   
        uart_send_package[UART_PROTCL_HEAD_OFFSET + buffer_offset] = UART_PROTCL_HEAD;
        uart_send_package[UART_PROTCL_TYPE_OFFSET + buffer_offset] = UART_PROTCL_TYPE_GCODE;
        uart_send_package[UART_PROTCL_DATALEN_OFFSET + buffer_offset] = dataLen & 0xff;
        uart_send_package[UART_PROTCL_DATALEN_OFFSET + buffer_offset + 1] = dataLen >> 8;
        
        strncpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + buffer_offset], dataField, dataLen);
        
        uart_send_package[dataLen + buffer_offset + 4] = UART_PROTCL_TAIL;

        uart_send_size = dataLen + 5;
    }
    
    
    
    if(monitor_tx_buf.length() + strlen(dataField) < 300)
    {
        monitor_tx_buf.concat(dataField);
    }
    else
    {
        //net_print((const uint8_t *)"overflow", strlen("overflow"));
    }
    return 0;
}



int package_file_first(File *fileHandle, char *fileName)
{
    int fileLen;
    char *ptr;
    int fileNameLen;
    int dataLen;
    char dbgStr[100] = {0};
    
    if(fileHandle == 0)
        return -1;
    fileLen = fileHandle->size();
    
    //net_print((const uint8_t *)"package_file_first:\n");
    
    //strcpy(fileName, (const char *)fileHandle->name());
    //sprintf(dbgStr, "fileLen:%d", fileLen);
    //net_print((const uint8_t *)dbgStr);
    //net_print((const uint8_t *)"\n");
    while(1)
    {
        ptr = (char *)strchr(fileName, '/');
        if(ptr == 0)
            break;
        else
        {
            strcpy(fileName, fileName + (ptr - fileName+ 1));
        }
    }
//  net_print((const uint8_t *)"fileName:");
    //net_print((const uint8_t *)fileName);
    //net_print((const uint8_t *)"\n");
    fileNameLen = strlen(fileName);

    dataLen = fileNameLen + 5;

    memset(uart_send_package, 0, sizeof(uart_send_package));
    uart_send_package[UART_PROTCL_HEAD_OFFSET] = UART_PROTCL_HEAD;
    uart_send_package[UART_PROTCL_TYPE_OFFSET] = UART_PROTCL_TYPE_FIRST;
    uart_send_package[UART_PROTCL_DATALEN_OFFSET] = dataLen & 0xff;
    uart_send_package[UART_PROTCL_DATALEN_OFFSET + 1] = dataLen >> 8;

    uart_send_package[UART_PROTCL_DATA_OFFSET] = fileNameLen;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 1] = fileLen & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 2] = (fileLen >> 8) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 3] = (fileLen >> 16) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 4] = (fileLen >> 24) & 0xff;
    strncpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + 5], fileName, fileNameLen);
    
    uart_send_package[dataLen + 4] = UART_PROTCL_TAIL;
    
    uart_send_size = dataLen + 5;

    return 0;
}

int package_file_first(char *fileName, int postLength)
{
    int fileLen;
    char *ptr;
    int fileNameLen;
    int dataLen;

    
    fileLen = postLength;
    
//  Serial.print("package_file_first:");
    
    while(1)
    {
        ptr = (char *)strchr(fileName, '/');
        if(ptr == 0)
            break;
        else
        {
            cut_msg_head((uint8_t *)fileName, strlen(fileName),  ptr - fileName+ 1);
        }
    }
//  Serial.print("fileName:");
//  Serial.println(fileName);
    
    fileNameLen = strlen(fileName);

    dataLen = fileNameLen + 5;

    memset(uart_send_package, 0, sizeof(uart_send_package));
    uart_send_package[UART_PROTCL_HEAD_OFFSET] = UART_PROTCL_HEAD;
    uart_send_package[UART_PROTCL_TYPE_OFFSET] = UART_PROTCL_TYPE_FIRST;
    uart_send_package[UART_PROTCL_DATALEN_OFFSET] = dataLen & 0xff;
    uart_send_package[UART_PROTCL_DATALEN_OFFSET + 1] = dataLen >> 8;

    uart_send_package[UART_PROTCL_DATA_OFFSET] = fileNameLen;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 1] = fileLen & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 2] = (fileLen >> 8) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 3] = (fileLen >> 16) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 4] = (fileLen >> 24) & 0xff;
    memcpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + 5], fileName, fileNameLen);
    
    uart_send_package[dataLen + 4] = UART_PROTCL_TAIL;
    
    uart_send_size = dataLen + 5;

    return 0;
}


int package_file_fragment(uint8_t *dataField, uint32_t fragLen, int32_t fragment)
{
    int dataLen;
    char dbgStr[100] = {0};

    dataLen = fragLen + 4;

    //sprintf(dbgStr, "fragment:%d\n", fragment);
    //net_print((const uint8_t *)dbgStr);
    
    memset(uart_send_package, 0, sizeof(uart_send_package));
    uart_send_package[UART_PROTCL_HEAD_OFFSET] = UART_PROTCL_HEAD;
    uart_send_package[UART_PROTCL_TYPE_OFFSET] = UART_PROTCL_TYPE_FRAGMENT;
    uart_send_package[UART_PROTCL_DATALEN_OFFSET] = dataLen & 0xff;
    uart_send_package[UART_PROTCL_DATALEN_OFFSET + 1] = dataLen >> 8;

    uart_send_package[UART_PROTCL_DATA_OFFSET] = fragment & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 1] = (fragment >> 8) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 2] = (fragment >> 16) & 0xff;
    uart_send_package[UART_PROTCL_DATA_OFFSET + 3] = (fragment >> 24) & 0xff;
    
    memcpy(&uart_send_package[UART_PROTCL_DATA_OFFSET + 4], (const char *)dataField, fragLen);
    
    uart_send_package[dataLen + 4] = UART_PROTCL_TAIL;
    
    uart_send_size = 1024;
    

    return 0;
}

unsigned long startTick = 0;
size_t readBytes;
uint8_t blockBuf[FILE_BLOCK_SIZE] = {0};
    

void do_transfer()
{
    
    char dbgStr[100] = {0};
    int i;
    long useTick ;
    long now;
    
    
    
    switch(transfer_state)
    {
        case TRANSFER_IDLE:
            if((uart_send_length_important > 0) || (uart_send_size > 0))
            {
                digitalWrite(EspReqTransferPin, LOW);
                if(digitalRead(McuTfrReadyPin) == LOW) // STM32 READY SIGNAL
                {
                    transfer_state = TRANSFER_FRAGMENT;
                }
                else
                    transfer_state = TRANSFER_READY;
            }
            
            break;
            
        case TRANSFER_GET_FILE:
            if(Serial.baudRate() != 1958400)
            {
                Serial.flush();
                Serial.updateBaudRate(1958400);
            }
            
             
            readBytes = gFileFifo.pop((char *)blockBuf, FILE_BLOCK_SIZE);
            if(readBytes > 0)
            {
                if(rcv_end_flag && (readBytes < FILE_BLOCK_SIZE))
                {
                    file_fragment |= (1 << 31); //the last fragment
                }
                else
                {
                    file_fragment &= ~(1 << 31);
                }

                package_file_fragment(blockBuf, readBytes, file_fragment);
            
                digitalWrite(EspReqTransferPin, LOW);
                
                transfer_state = TRANSFER_READY;

                file_fragment++;

                
            }
            else if(rcv_end_flag)
            {
                memset(blockBuf, 0, sizeof(blockBuf));
                readBytes = 0;
                file_fragment |= (1 << 31); //the last fragment

                package_file_fragment(blockBuf, readBytes, file_fragment);
            
                digitalWrite(EspReqTransferPin, LOW);
                
                transfer_state = TRANSFER_READY;
            }

            
            
            break;

        case TRANSFER_READY:
                        
            if(digitalRead(McuTfrReadyPin) == LOW) // STM32 READY SIGNAL
            {
                transfer_state = TRANSFER_FRAGMENT;
            }
                
            break;
            
        case TRANSFER_FRAGMENT:
                
            if(uart_send_length_important > 0)
            {
                uart_send_length_important = (uart_send_length_important >= sizeof(uart_send_package_important) ? sizeof(uart_send_package_important) : uart_send_length_important);
                Serial.write(uart_send_package_important, uart_send_length_important);
                uart_send_length_important = 0;
                memset(uart_send_package_important, 0, sizeof(uart_send_package_important));
            }
            else
            {
                Serial.write(uart_send_package, uart_send_size);
                uart_send_size = 0;
                memset(uart_send_package, 0, sizeof(uart_send_package));
            }
            

            
            digitalWrite(EspReqTransferPin, HIGH);

            if(!transfer_file_flag)
            {
                transfer_state = TRANSFER_IDLE;
            }
            else
            {
                if(rcv_end_flag && (readBytes < FILE_BLOCK_SIZE))
                {                           
                    
                    if(Serial.baudRate() != 115200)
                    {
                        Serial.flush();
                         Serial.updateBaudRate(115200);
                    }
                    transfer_file_flag = false;
                    rcv_end_flag = false;
                    transfer_state = TRANSFER_IDLE;

                    
                }
                else
                {
                    transfer_state = TRANSFER_GET_FILE;
                }
            }
                
            break;
            
        default:
            break;



    }
    if(transfer_file_flag)
    {
        if((gFileFifo.left() >= TCP_FRAG_LEN) && (transfer_frags >= TCP_FRAG_LEN))
        {
            net_print((const uint8_t *) "ok\n", strlen((const char *)"ok\n"));
            transfer_frags -= TCP_FRAG_LEN;
        }
    }
}


/*******************************************************************
    receive data from stm32 handler

********************************************************************/
#define UART_RX_BUFFER_SIZE    1024

#define ESP_PROTOC_HEAD (uint8_t)0xa5
#define ESP_PROTOC_TAIL     (uint8_t)0xfc

#define ESP_TYPE_NET            (uint8_t)0x0
#define ESP_TYPE_PRINTER        (uint8_t)0x1
#define ESP_TYPE_TRANSFER       (uint8_t)0x2
#define ESP_TYPE_EXCEPTION      (uint8_t)0x3
#define ESP_TYPE_CLOUD          (uint8_t)0x4
#define ESP_TYPE_UNBIND     (uint8_t)0x5
#define ESP_TYPE_WID            (uint8_t)0x6
#define ESP_TYPE_SCAN_WIFI      (uint8_t)0x7
#define ESP_TYPE_MANUAL_IP      (uint8_t)0x8
#define ESP_TYPE_WIFI_CTRL      (uint8_t)0x9


uint8_t esp_msg_buf[UART_RX_BUFFER_SIZE] = {0}; //麓忙麓垄麓媒麓娄脌铆碌脛脢媒戮脻
uint16_t esp_msg_index = 0; //脨麓脰赂脮毛

typedef struct
{
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
    for(i = 0; i < _arrayLen; i++)
    {
        if(*(_array + i) == _char)
        {
            return i;
        }
    }
    
    return -1;
}

static int cut_msg_head(uint8_t *msg, uint16_t msgLen, uint16_t cutLen)
{
    int i;
    
    if(msgLen < cutLen)
    {
        return 0;
    }
    else if(msgLen == cutLen)
    {
        memset(msg, 0, msgLen);
        return 0;
    }
    for(i = 0; i < (msgLen - cutLen); i++)
    {
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

    if(msgLen <= 0)
        return;

    //0x01:AP
    //0x02:Client
    //0x03:AP+Client(?Y2??隆矛3?)
    if((msg[0] != 0x01) && (msg[0] != 0x02)) 
        return;
    cfg_mode = msg[0];

    if(msg[1] > 32)
        return;
    cfg_wifi_len = msg[1];
    cfg_wifi = &msg[2];
    
    if(msg[2 +cfg_wifi_len ] > 64)
        return;
    cfg_key_len = msg[2 +cfg_wifi_len];
    cfg_key = &msg[3 +cfg_wifi_len];

    
    
    if((cfg_mode == 0x01) && ((currentState == OperatingState::Client) 
        || (cfg_wifi_len != strlen((const char *)softApName))
        || (strncmp((const char *)cfg_wifi, (const char *)softApName, cfg_wifi_len) != 0)
        || (cfg_key_len != strlen((const char *)softApKey))
        || (strncmp((const char *)cfg_key,  (const char *)softApKey, cfg_key_len) != 0)))
    {
        if((cfg_key_len > 0) && (cfg_key_len < 8))
        {
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
    }
    else if((cfg_mode == 0x02) && ((currentState == OperatingState::AccessPoint)
        || (cfg_wifi_len != strlen((const char *)ssid))
        || (strncmp((const char *)cfg_wifi, (const char *)ssid, cfg_wifi_len) != 0)
        || (cfg_key_len != strlen((const char *)pass))
        || (strncmp((const char *)cfg_key,  (const char *)pass, cfg_key_len) != 0)))
    {
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

static void cloud_msg_handle(uint8_t * msg, uint16_t msgLen)
{
//Todo
}


static void scan_wifi_msg_handle()
{
    uint8_t valid_nums = 0;
    uint32_t byte_offset = 1;
    uint8_t node_lenth;
    int8_t signal_rssi;
    
    if(currentState == OperatingState::AccessPoint)
    {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);
    }
    
    int n = WiFi.scanNetworks();
//  Serial.println("scan done");
    if (n == 0)
    {
        //Serial.println("no networks found");
        return;
    }
    else
    {
        int index = 0;
        //Serial.print(n);
        //Serial.println(" networks found");
        memset(uart_send_package, 0, sizeof(uart_send_package));
        uart_send_package[UART_PROTCL_HEAD_OFFSET] = UART_PROTCL_HEAD;
        uart_send_package[UART_PROTCL_TYPE_OFFSET] = UART_PROTCL_TYPE_HOT_PORT;
        for (int i = 0; i < n; ++i)
        {
            if(valid_nums > 15)
                break;
            signal_rssi = (int8_t)WiFi.RSSI(i);
            // Print SSID and RSSI for each network found
            /*Serial.print(i + 1);
            Serial.print(": ");
            Serial.print(WiFi.SSID(i));
            Serial.print(" (");
            Serial.print(WiFi.RSSI(i));
            Serial.print(")");
            Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");
            delay(10);*/
            node_lenth = (uint8_t)WiFi.SSID(i).length();
            if(node_lenth > 32)
            {                   
                continue;
            }   
            if(signal_rssi < -78)
            {
                continue;
            }

            uart_send_package[UART_PROTCL_DATA_OFFSET + byte_offset] = node_lenth;
            WiFi.SSID(i).toCharArray(&uart_send_package[UART_PROTCL_DATA_OFFSET + byte_offset + 1], node_lenth + 1, 0);
            uart_send_package[UART_PROTCL_DATA_OFFSET + byte_offset + node_lenth + 1] = WiFi.RSSI(i);

            valid_nums++;
            byte_offset += node_lenth + 2;
            
        }
        
        uart_send_package[UART_PROTCL_DATA_OFFSET] = valid_nums;
        uart_send_package[UART_PROTCL_DATA_OFFSET + byte_offset] = UART_PROTCL_TAIL;
        uart_send_package[UART_PROTCL_DATALEN_OFFSET] = byte_offset & 0xff;
        uart_send_package[UART_PROTCL_DATALEN_OFFSET + 1] = byte_offset >> 8;

        uart_send_size = byte_offset + 5;

        /*if(transfer_state == TRANSFER_IDLE)
        {
            transfer_state = TRANSFER_READY;
            digitalWrite(EspReqTransferPin, LOW);
        }*/
        
    }
    //Serial.println("");
}


static void manual_ip_msg_handle(uint8_t * msg, uint16_t msgLen)
{
    
    if(msgLen < 16)
        return;
        
    ip_static = (msg[3] << 24) + (msg[2] << 16) + (msg[1] << 8) + msg[0];
    subnet_static = (msg[7] << 24) + (msg[6] << 16) + (msg[5] << 8) + msg[4];
    gateway_staic = (msg[11] << 24) + (msg[10] << 16) + (msg[9] << 8) + msg[8];
    dns_static = (msg[15] << 24) + (msg[14] << 16) + (msg[13] << 8) + msg[12];

    manual_valid = 0xa;
    
    WiFi.config(ip_static, gateway_staic, subnet_static, dns_static, (uint32_t)0x00000000);

    EEPROM.put(BAK_ADDRESS_MANUAL_IP, ip_static);
    EEPROM.put(BAK_ADDRESS_MANUAL_MASK, subnet_static);
    EEPROM.put(BAK_ADDRESS_MANUAL_GATEWAY, gateway_staic);
    EEPROM.put(BAK_ADDRESS_MANUAL_DNS, dns_static);
    EEPROM.put(BAK_ADDRESS_MANUAL_IP_FLAG, manual_valid);

    EEPROM.commit();

    
    
}

static void wifi_ctrl_msg_handle(uint8_t * msg, uint16_t msgLen)
{
    if(msgLen != 1)
        return;

    uint8_t ctrl_code = msg[0];

    /*connect the wifi network*/
    if(ctrl_code == 0x1)
    {
        if(!WiFi.isConnected())
        {
            WiFi.begin(ssid, pass);
        }
    }
    /*disconnect the wifi network*/
    else if(ctrl_code == 0x2)
    {
        if(WiFi.isConnected())
        {
            WiFi.disconnect();
        }
    }
    /*disconnect the wifi network and forget the password*/
    else if(ctrl_code == 0x3)
    {
        if(WiFi.isConnected())
        {
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

static void except_msg_handle(uint8_t * msg, uint16_t msgLen)
{
    uint8_t except_code = msg[0];
    if(except_code == 0x1) // transfer error
    {
        upload_error = true;
    }
    else if(except_code == 0x2) // transfer sucessfully
    {
        upload_success = true;
    }
}

static void wid_msg_handle(uint8_t * msg, uint16_t msgLen)
{
//Todo
}

static void transfer_msg_handle(uint8_t * msg, uint16_t msgLen)
{
    int j = 0;
    char cmd_line[100] = {0};
    
    init_queue(&cmd_queue);
    cmd_index = 0;
    memset(cmd_fifo, 0, sizeof(cmd_fifo));
    
    while(j < msgLen)
    {
        if((msg[j] == '\r') || (msg[j] == '\n'))
        {
            if((cmd_index) > 1)
            {
                cmd_fifo[cmd_index] = '\n';
                cmd_index++;

                
                push_queue(&cmd_queue, cmd_fifo, cmd_index);
            }
            memset(cmd_fifo, 0, sizeof(cmd_fifo));
            cmd_index = 0;
    //  net_print((const uint8_t*)"push:", strlen((const char *)"push:"));
    //  net_print((const uint8_t*)cmd_fifo, strlen((const char *)cmd_fifo));
        }
        else if(msg[j] == '\0')
            break;
        else
        {
            if(cmd_index >= sizeof(cmd_fifo))
            {
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
    while(pop_queue(&cmd_queue, cmd_line, sizeof(cmd_line)) >= 0)       
    {
        if(monitor_rx_buf.length() + strlen(cmd_line) < 500)
        {
            monitor_rx_buf.concat((const char *)cmd_line);
        }
        else
        {
            //net_print((const uint8_t *)"rx overflow", strlen("rx overflow"));
        }
        /*
        if((cmd_line[0] == 'o') && (cmd_line[1] == 'k'))
        {
            cut_msg_head((uint8_t *)cmd_line, strlen((const char*)cmd_line), 2);
            //if(strlen(cmd_line) < 4)
                continue;
        }*/

        /*handle the cmd*/
        paser_cmd((uint8_t *)cmd_line);
        do_transfer();
        yield();
        
        if((cmd_line[0] == 'T') && (cmd_line[1] == ':'))
        {       
            String tempVal((const char *)cmd_line);
            int index = tempVal.indexOf("B:", 0);
            if(index != -1)         
            {
                memset(dbgStr, 0, sizeof(dbgStr));
                sprintf((char *)dbgStr, "T:%d /%d B:%d /%d T0:%d /%d T1:%d /%d @:0 B@:0\r\n", 
                    (int)gPrinterInf.curSprayerTemp[0], (int)gPrinterInf.desireSprayerTemp[0], (int)gPrinterInf.curBedTemp, (int)gPrinterInf.desireBedTemp,
                    (int)gPrinterInf.curSprayerTemp[0], (int)gPrinterInf.desireSprayerTemp[0], (int)gPrinterInf.curSprayerTemp[1], (int)gPrinterInf.desireSprayerTemp[1]);
                net_print((const uint8_t*)dbgStr, strlen((const char *)dbgStr));
                    
            }
            continue;
        }
        else if((cmd_line[0] == 'M') && (cmd_line[1] == '9') && (cmd_line[2] == '9') 
            && ((cmd_line[3] == '7') ||  (cmd_line[3] == '2') ||  (cmd_line[3] == '4')))
        {
            continue;
        }
        else if((cmd_line[0] == 'M') && (cmd_line[1] == '2') && (cmd_line[2] == '7'))
        {
            continue;
        }
        else
        {
            net_print((const uint8_t*)cmd_line, strlen((const char *)cmd_line));
        }
        
        
    }
    
    
    
}

void esp_data_parser(char *cmdRxBuf, int len)
{
    int32_t head_pos;
    int32_t tail_pos;
    uint16_t cpyLen;
    int16_t leftLen = len; //脢拢脫脿鲁陇露脠
    uint8_t loop_again = 0;
    int i;

    ESP_PROTOC_FRAME esp_frame;

    
    //net_print((const uint8_t *)"rcv:");

    //net_print((const uint8_t *)"\n");
    
    
    while((leftLen > 0) || (loop_again == 1))
    //while(leftLen > 0)
    {
        loop_again = 0;
        
        /* 1. 虏茅脮脪脰隆脥路*/
        if(esp_msg_index != 0)
        {
            head_pos = 0;
            cpyLen = (leftLen < (sizeof(esp_msg_buf) - esp_msg_index)) ? leftLen : sizeof(esp_msg_buf) - esp_msg_index;
            
            memcpy(&esp_msg_buf[esp_msg_index], cmdRxBuf + len - leftLen, cpyLen);          

            esp_msg_index += cpyLen;

            leftLen = leftLen - cpyLen;
            tail_pos = charAtArray(esp_msg_buf, esp_msg_index, ESP_PROTOC_TAIL);
            
        //  net_print((const uint8_t *)esp_msg_buf, esp_msg_index); 
            if(tail_pos == -1)
            {
                //脙禄脫脨脰隆脦虏
                if(esp_msg_index >= sizeof(esp_msg_buf))
                {
                    memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
                    esp_msg_index = 0;
                }
            
                return;
            }
        }
        else
        {
            head_pos = charAtArray((uint8_t const *)&cmdRxBuf[len - leftLen], leftLen, ESP_PROTOC_HEAD);
        //  net_print((const uint8_t *)"esp_data_parser1\n");
            if(head_pos == -1)
            {
                //脙禄脫脨脰隆脥路
                return;
            }
            else
            {
                //脧脠禄潞麓忙碌陆buf   
                memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
                memcpy(esp_msg_buf, &cmdRxBuf[len - leftLen + head_pos], leftLen - head_pos);

                esp_msg_index = leftLen - head_pos;


                leftLen = 0;

                head_pos = 0;
                
                tail_pos = charAtArray(esp_msg_buf, esp_msg_index, ESP_PROTOC_TAIL);
                //net_print((const uint8_t *)"esp_data_parser2\n", strlen((const char *)"esp_data_parser2\n"));
                if(tail_pos == -1)
                {
                    //脮脪碌陆脰隆脥路拢卢脙禄脫脨脰隆脦虏        
                    return;
                }
                
            }
        }
        //net_print((const uint8_t *)"esp_data_parser3\n");
        /*3. 脮脪碌陆脥锚脮没碌脛脪禄脰隆 , 脜脨露脧脢媒戮脻鲁陇露脠*/
        esp_frame.type = esp_msg_buf[1];
    
        if((esp_frame.type != ESP_TYPE_NET) && (esp_frame.type != ESP_TYPE_PRINTER)
             && (esp_frame.type != ESP_TYPE_CLOUD) && (esp_frame.type != ESP_TYPE_UNBIND)
             && (esp_frame.type != ESP_TYPE_TRANSFER) && (esp_frame.type != ESP_TYPE_EXCEPTION) 
             && (esp_frame.type != ESP_TYPE_WID) && (esp_frame.type != ESP_TYPE_SCAN_WIFI)
             && (esp_frame.type != ESP_TYPE_MANUAL_IP) && (esp_frame.type != ESP_TYPE_WIFI_CTRL))
        {
            //脢媒戮脻脌脿脨脥虏禄脮媒脠路拢卢露陋脝煤
            memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
            esp_msg_index = 0;
            //net_print((const uint8_t *)"type err\n", strlen("type err\n"));
            return;
        }
        //net_print((const uint8_t *)"esp_data_parser4\n");
        esp_frame.dataLen = esp_msg_buf[2] + (esp_msg_buf[3] << 8);

        /*脢媒戮脻鲁陇露脠虏禄脮媒脠路*/
        if(4 + esp_frame.dataLen > sizeof(esp_msg_buf))
        {
            //脢媒戮脻鲁陇露脠虏禄脮媒脠路拢卢露陋脝煤
            memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
            esp_msg_index = 0;
            //net_print((const uint8_t *)"len err\n", strlen("len err\n"));
            return;
        }

        if(esp_msg_buf[4 + esp_frame.dataLen] != ESP_PROTOC_TAIL)
        {
            //脰隆脦虏虏禄脮媒脠路拢卢露陋脝煤
            memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
            //net_print((const uint8_t *)"tail err\n", strlen("tail err\n"));
            esp_msg_index = 0;
            return;
        }
    
        /*4. 掳麓脮脮脌脿脨脥路脰卤冒麓娄脌铆脢媒戮脻*/     
        esp_frame.data = &esp_msg_buf[4];

        
        
    
        switch(esp_frame.type)
        {
            case ESP_TYPE_NET:
                net_msg_handle(esp_frame.data, esp_frame.dataLen);
                break;

            case ESP_TYPE_PRINTER:
                //gcode_msg_handle(esp_frame.data, esp_frame.dataLen);
                break;

            case ESP_TYPE_TRANSFER:
            //  net_print((const uint8_t *)"ESP_TYPE_TRANSFER", strlen((const char *)"ESP_TYPE_TRANSFER"));
                if(verification_flag)
                    transfer_msg_handle(esp_frame.data, esp_frame.dataLen);
                break;

            case ESP_TYPE_CLOUD:
                if(verification_flag)
                    cloud_msg_handle(esp_frame.data, esp_frame.dataLen);
                break;

            case ESP_TYPE_EXCEPTION:
                
                except_msg_handle(esp_frame.data, esp_frame.dataLen);
                break;

            case ESP_TYPE_UNBIND:
                if(cloud_link_state == 3)
                {
                    unbind_exec = 1;
                }
                break;
            case ESP_TYPE_WID:
                wid_msg_handle(esp_frame.data, esp_frame.dataLen);
                break;

            case ESP_TYPE_SCAN_WIFI:
                
                scan_wifi_msg_handle();
                break;

            case ESP_TYPE_MANUAL_IP:                
                manual_ip_msg_handle(esp_frame.data, esp_frame.dataLen);
                break;

            case ESP_TYPE_WIFI_CTRL:                
                wifi_ctrl_msg_handle(esp_frame.data, esp_frame.dataLen);
                break;
            
            default:
                break;              
        }
        /*5. 掳脩脪脩麓娄脌铆碌脛脢媒戮脻陆脴碌么*/
        esp_msg_index = cut_msg_head(esp_msg_buf, esp_msg_index, esp_frame.dataLen  + 5);
        if(esp_msg_index > 0)
        {
            if(charAtArray(esp_msg_buf, esp_msg_index,  ESP_PROTOC_HEAD) == -1)
            {
                memset(esp_msg_buf, 0, sizeof(esp_msg_buf));
                esp_msg_index = 0;
                return;
            }
            
            if((charAtArray(esp_msg_buf, esp_msg_index,  ESP_PROTOC_HEAD) != -1) && (charAtArray(esp_msg_buf, esp_msg_index, ESP_PROTOC_TAIL) != -1))
            {
                loop_again = 1;
            }
        }
        yield();
    
    }
}



// Try to connect using the saved SSID and password, returning true if successful
bool TryToConnect()
{

    char eeprom_valid[1] = {0};
    uint8_t failcount = 0;
    
    EEPROM.get(BAK_ADDRESS_WIFI_VALID, eeprom_valid);
    if(eeprom_valid[0] == 0x0a)
    {   
        log_esp3d("EEPROM is valid");
        log_esp3d("Read SSID/Password from EEPROM");
        EEPROM.get(BAK_ADDRESS_WIFI_MODE, wifi_mode);       
        EEPROM.get(BAK_ADDRESS_WEB_HOST, webhostname);
        log_esp3d("Mode:%s, web hostname:%s", wifi_mode,webhostname);
    }
    else
    {
        log_esp3d("EEPROM is not valid, reset it");
        memset(wifi_mode, 0, sizeof(wifi_mode));
        strcpy(wifi_mode, "wifi_mode_ap");
        log_esp3d("Mode:%s", wifi_mode);
        NET_INF_UPLOAD_CYCLE = 1000;
    }
    
    

    if(strcmp(wifi_mode, "wifi_mode_ap") != 0)
    {   
        log_esp3d("mode is NOT ap");
        if(eeprom_valid[0] == 0x0a)
        {
            log_esp3d("EEPROM is valid");
            log_esp3d("Read SSID/Password from EEPROM");
            EEPROM.get(BAK_ADDRESS_WIFI_SSID, ssid);
            EEPROM.get(BAK_ADDRESS_WIFI_KEY, pass);
            log_esp3d("SSID:%s, pass:%s", ssid, pass);
        }
        else
        {
            log_esp3d("EEPROM is not valid, reset it");
            memset(ssid, 0, sizeof(ssid));
            strcpy(ssid, "mks1");
            memset(pass, 0, sizeof(pass));
            strcpy(pass, "makerbase");
            log_esp3d("SSID:%s, pass:%s", ssid,pass);
        }
        

        currentState = OperatingState::Client;
        log_esp3d("Current state is client :%d", currentState);
        package_net_para();
        log_esp3d("Sending Net Frame");
        Serial.write(uart_send_package, uart_send_size);        
        log_esp3d("Transfert state is: Ready");
        transfer_state = TRANSFER_READY;
        log_esp3d("Wait 10s");
        delay(1000);
        
        log_esp3d("Setup WiFi as STA");
        WiFi.mode(WIFI_STA);
        log_esp3d("Disconnect from any AP");
        WiFi.disconnect();
        
        delay(1000);
        
        log_esp3d("Check if static IP");
        EEPROM.get(BAK_ADDRESS_MANUAL_IP_FLAG, manual_valid);
        
        if(manual_valid == 0xa)
        {
            uint32_t manual_ip, manual_subnet, manual_gateway, manual_dns;
            
            EEPROM.get(BAK_ADDRESS_MANUAL_IP, ip_static);
            EEPROM.get(BAK_ADDRESS_MANUAL_MASK, subnet_static);
            EEPROM.get(BAK_ADDRESS_MANUAL_GATEWAY, gateway_staic);
            EEPROM.get(BAK_ADDRESS_MANUAL_DNS, dns_static);
            log_esp3d("Use Static IP");
            WiFi.config(ip_static, gateway_staic, subnet_static, dns_static, (uint32_t)0x00000000);
        }
        
        log_esp3d("Setup WiFi as STA, SSID:%s, PWD:%s", ssid, pass);
        WiFi.begin(ssid, pass);

        log_esp3d("Connecting");
        while (WiFi.status() != WL_CONNECTED)
        {
            if(get_printer_reply() > 0)
            {   log_esp3d("Read incoming data");
                esp_data_parser((char *)uart_rcv_package, uart_rcv_index);
            }
            uart_rcv_index = 0;
            package_net_para();
            log_esp3d("Sending Net Frame");
            Serial.write(uart_send_package, uart_send_size);
            
            delay(500);
            
            failcount++;
            
            if (failcount > MAX_WIFI_FAIL)  // 1 min
            {
              delay(100);
              log_esp3d("Timeout");
              return false;
            }
            log_esp3d("Do transfer");
            do_transfer();
            
        };
    } else
    {
       log_esp3d("mode is ap");
        if(eeprom_valid[0] == 0x0a)
        {
            log_esp3d("EEPROM is valid");
            log_esp3d("Read SSID/Password from EEPROM");
            EEPROM.get(BAK_ADDRESS_WIFI_SSID, softApName);
            EEPROM.get(BAK_ADDRESS_WIFI_KEY, softApKey);
            log_esp3d("SSID:%s, pass:%s", softApName,softApKey);
        }
        else
        {   
            log_esp3d("EEPROM is not valid, reset it");
            String macStr= WiFi.macAddress();
            macStr.replace(":", "");
            strcat(softApName, macStr.substring(8).c_str());
            memset(pass, 0, sizeof(pass));
             log_esp3d("SSID:%s, no password", softApName);
        }
        currentState = OperatingState::AccessPoint;
        log_esp3d("Current state is Access point :%d", currentState);
        
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        if(strlen(softApKey) != 0)
            WiFi.softAP(softApName, softApKey);
        else
            WiFi.softAP(softApName);
            log_esp3d("Setup WiFi as AP, SSID:%s, PWD:%s", softApName, softApKey);
    }
    return true;
}

uint8_t refreshApWeb()
{
    wifiConfigHtml = F("<html><head><meta http-equiv='Content-Type' content='text/html;'><title>MKS WIFI</title><style>body{background: #b5ff6a;}.config{margin: 150px auto;width: 600px;height: 600px;overflow: hidden;</style></head>");
    wifiConfigHtml += F("<body><div class='config'></caption><br /><h2>Update</h2>");
    wifiConfigHtml += F("<form method='POST' action='update_sketch' enctype='multipart/form-data'><table border='0'><tr><td>wifi firmware:</td><td><input type='file' name='update' ></td><td><input type='submit' value='update'></td></tr></form>");
    wifiConfigHtml += F("<form method='POST' action='update_fs' enctype='multipart/form-data'><tr><td>web view:</td><td><input type='file' name='update' ></td><td><input type='submit' value='update'></td></tr></table></form>");
    wifiConfigHtml += F("<br /><br /><h2>WIFI Configuration</h2><form method='GET' action='update_cfg'><caption><input type='radio' id='wifi_mode_sta' name='wifi_mode' value='wifi_mode_sta' /><label for='wifi_mode_sta'>STA</label><br />");
    wifiConfigHtml += F("<input type='radio' id='wifi_mode_ap' name='wifi_mode' value='wifi_mode_ap' /><label for='wifi_mode_ap'>AP</label><br /><br /><table border='0'><tr><td>");
    wifiConfigHtml += F("WIFI: </td><td><input type='text' id='hidden_ssid' name='hidden_ssid' /></td></tr><tr><td>KEY: </td><td><input type='"); 
#ifdef SHOW_PASSWORDS
    wifiConfigHtml += F("text");
#else
    wifiConfigHtml += F("password");
#endif
    wifiConfigHtml += F("' id='password' name='password' />");
    wifiConfigHtml += F("</td></tr><tr><td colspan=2 align='right'> <input type='submit' value='config and reboot'></td></tr></table></form></div></body></html>");
    return 0;
}

char hidden_ssid[32] = {0};

void onWifiConfig()
{
    uint8_t num_ssids = refreshApWeb();

    server.on("/", HTTP_GET, []() {
        server.send(200, FPSTR(STR_MIME_TEXT_HTML), wifiConfigHtml);
    });
    server.on("/update_sketch", HTTP_GET, []() {
        server.send(200, FPSTR(STR_MIME_TEXT_HTML), wifiConfigHtml);
    });
    server.on("/update_fs", HTTP_GET, []() {
        server.send(200, FPSTR(STR_MIME_TEXT_HTML), wifiConfigHtml);
    });

 
    server.on("/update_cfg", HTTP_GET, []() {
        if (server.args() <= 0) 
        {
            server.send(500, FPSTR(STR_MIME_TEXT_PLAIN), F("Got no data, go back and retry"));
            return;
        }
        for (uint8_t e = 0; e < server.args(); e++) {
            String argument = server.arg(e);
            urldecode(argument);
            if (server.argName(e) == "password") argument.toCharArray(pass, 64);
            else if (server.argName(e) == "ssid") argument.toCharArray(ssid, 32);
            else if (server.argName(e) == "hidden_ssid") argument.toCharArray(hidden_ssid, 32);
            else if (server.argName(e) == "wifi_mode") argument.toCharArray(wifi_mode, 15);
        }

        if(strlen((const char *)hidden_ssid) <= 0)
        {
            server.send(200, FPSTR(STR_MIME_TEXT_HTML), F("<p>wifi parameters error!</p>"));
            return;
        }
        if((strcmp(wifi_mode, "wifi_mode_ap") == 0) && (strlen(pass) > 0) && ((strlen(pass) < 8) ))
        {
            server.send(500, FPSTR(STR_MIME_TEXT_PLAIN), F("wifi password length is not correct, go back and retry"));
            return; 
        }
        else
        {
            memset(ssid, 0, sizeof(ssid));
            memcpy(ssid, hidden_ssid, sizeof(hidden_ssid));
        }
        
        char valid[1] = {0x0a};
        
        EEPROM.put(BAK_ADDRESS_WIFI_SSID, ssid);
        EEPROM.put(BAK_ADDRESS_WIFI_KEY, pass);
        EEPROM.put(BAK_ADDRESS_WEB_HOST, webhostname);

        EEPROM.put(BAK_ADDRESS_WIFI_MODE, wifi_mode);

        EEPROM.put(BAK_ADDRESS_WIFI_VALID, valid);

        EEPROM.put(BAK_ADDRESS_MANUAL_IP_FLAG, 0xff);
        manual_valid = 0xff;

        EEPROM.commit();

        server.send(200, FPSTR(STR_MIME_TEXT_HTML), F("<p>Configure successfully!<br />Please use the new ip to connect again.</p>"));
    
        delay(300);
        ESP.restart();
    });
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

    onWifiConfig();
    
  
  server.begin();
}

static void extract_file_item(File dataFile, String fileStr)
{
//Todo
}
static void extract_file_item_cloud(File dataFile, String fileStr)
{
//Todo
}
void fsHandler()
{
    String path = server.uri();

    if(!verification_flag)
    {
        return;
    }

    bool addedGz = false;
    File dataFile = LittleFS.open(path, "r");

    if (!dataFile && !path.endsWith(".gz") && path.length() <= 29)
    {
        // Requested file not found and wasn't a zipped file, so see if we have a zipped version
        path += F(".gz");
        addedGz = true;
        dataFile = LittleFS.open(path, "r");
    }
    if (!dataFile)
    {
        server.send(404, FPSTR(STR_MIME_APPLICATION_JSON), "{\"err\": \"404: " + server.uri() + " NOT FOUND\"}");
        return;
    }
    // No need to add the file size or encoding headers here because streamFile() does that automatically
    String dataType = FPSTR(STR_MIME_TEXT_PLAIN);
    if (path.endsWith(".html") || path.endsWith(".htm")) dataType = FPSTR(STR_MIME_TEXT_HTML);
    else if (path.endsWith(".css") || path.endsWith(".css.gz")) dataType = F("text/css");
    else if (path.endsWith(".js") || path.endsWith(".js.gz")) dataType = F("application/javascript");
    else if (!addedGz && path.endsWith(".gz")) dataType = F("application/x-gzip");
    else if ( path.endsWith(".png")) dataType = F("application/x-png");
    else if ( path.endsWith(".ico")) dataType = F("image/x-icon");

    server.streamFile(dataFile, dataType);


    dataFile.close();

}

void handleUpload()
{


  uint32_t now;
  uint8_t readBuf[1024];

  uint32_t postLength = server.getPostLength();
  String uri = server.uri();
  

  if(uri != NULL)
  {
    if((transfer_file_flag) || (transfer_state != TRANSFER_IDLE) || (gPrinterInf.print_state != PRINTER_IDLE))
    {   
        server.send(409, FPSTR(STR_MIME_TEXT_PLAIN), FPSTR("409 Conflict"));    
        return;
    }
        
    if(server.hasArg((const char *) "X-Filename"))
    {
        if((transfer_file_flag) || (transfer_state != TRANSFER_IDLE))
        {
            server.send(500, FPSTR(STR_MIME_APPLICATION_JSON), FPSTR(STR_JSON_ERR_500_IS_BUSY));        
            return;
        }

        file_fragment = 0;
        rcv_end_flag = false;
        transfer_file_flag = true;
        gFileFifo.reset();
        upload_error = false;
        upload_success = false;

        String FileName = server.arg((const char *) "X-Filename");
        //package_gcode(FileName, true);
        //transfer_state = TRANSFER_READY;
        //digitalWrite(EspReqTransferPin, LOW);
        //String fileNameAfterDecode = urlDecode(FileName);
        //package_gcode(fileNameAfterDecode, true);
        //transfer_state = TRANSFER_READY;
        //digitalWrite(EspReqTransferPin, LOW);
        if(package_file_first((char *)FileName.c_str(), (int)postLength) == 0)
        {
            /*transfer_state = TRANSFER_READY;
            digitalWrite(EspReqTransferPin, LOW);*/
        }
        else
        {
            transfer_file_flag = false;
        }
        /*wait m3 reply for first frame*/
        int wait_tick = 0;
        while(1)
        {
            do_transfer();
            
            delay(100);

            wait_tick++;

            if(wait_tick > 20) // 2s
            {
                if(digitalRead(McuTfrReadyPin) == HIGH) // STM32 READY SIGNAL
                {
                    upload_error = true;        
                //  Serial.println("upload_error");
                }
                else
                {
            //      Serial.println("upload_sucess");
                }
                break;
            }
            
            int len_get = get_printer_reply();
            if(len_get > 0)
            {
                esp_data_parser((char *)uart_rcv_package, len_get);
            
                uart_rcv_index = 0;
            }

            if(upload_error)
            {
                break;
            }
        }
        
        if(!upload_error)
        {
            
             now = millis();
            do
            {
                do_transfer();

                int len = get_printer_reply();
            
                if(len > 0)
                {
                //  Serial.println("rcv");
                    esp_data_parser((char *)uart_rcv_package, len);
                
                    uart_rcv_index = 0;
                }

                if(upload_error || upload_success)
                {
                    break;
                }

                if (postLength != 0)
                {                 
                    uint32_t len = gFileFifo.left();



                    if (len > postLength)
                    {
                         len = postLength;
                    }
                    if(len > sizeof(readBuf))
                    {
                         len = sizeof(readBuf);
                    }   
                    if(len > 0)
                    {

                        size_t len2 = server.readPostdata(server.client(), readBuf, len);
                
                        if (len2 > 0)
                        {
                            postLength -= len2;

                            gFileFifo.push((char *)readBuf, len2);

                            
                            now = millis();
                        }
                    }
                    
                }
                else
                {
                    rcv_end_flag = true;
                    break;
                }
                yield();
            
                
            }while (millis() - now < 10000);
        }
        
        if(upload_success || rcv_end_flag )
        {
            server.send(200, FPSTR(STR_MIME_APPLICATION_JSON), FPSTR(STR_JSON_ERR_0));  
        }  else
        {
            if(Serial.baudRate() != 115200)
            {
                Serial.flush();
                Serial.begin(115200);
              }
            transfer_file_flag = false;
            rcv_end_flag = false;
            transfer_state = TRANSFER_IDLE;
            server.send(500, FPSTR(STR_MIME_APPLICATION_JSON), FPSTR(STR_JSON_ERR_500_NO_DATA_RECEIVED));   
        }
        
    }
    else
    {
        
        server.send(500, FPSTR(STR_MIME_APPLICATION_JSON), FPSTR(STR_JSON_ERR_500_NO_FILENAME_PROVIDED));   
        return;
    }
    

         
  }

  

}


void handleRrUpload() {
}



void cloud_down_file()
{
//Todo
}


void cloud_get_file_list()
{
//Todo
}

void cloud_handler()
{
//Todo
}

void urldecode(String &input) { // LAL ^_^
  input.replace("%0A", String('\n'));
  input.replace("%20", " ");
  input.replace("+", " ");
  input.replace("%21", "!");
  input.replace("%22", "\"");
  input.replace("%23", "#");
  input.replace("%24", "$");
  input.replace("%25", "%");
  input.replace("%26", "&");
  input.replace("%27", "\'");
  input.replace("%28", "(");
  input.replace("%29", ")");
  input.replace("%30", "*");
  input.replace("%31", "+");
  input.replace("%2C", ",");
  input.replace("%2E", ".");
  input.replace("%2F", "/");
  input.replace("%2C", ",");
  input.replace("%3A", ":");
  input.replace("%3A", ";");
  input.replace("%3C", "<");
  input.replace("%3D", "=");
  input.replace("%3E", ">");
  input.replace("%3F", "?");
  input.replace("%40", "@");
  input.replace("%5B", "[");
  input.replace("%5C", "\\");
  input.replace("%5D", "]");
  input.replace("%5E", "^");
  input.replace("%5F", "-");
  input.replace("%60", "`");
  input.replace("%7B", "{");
  input.replace("%7D", "}");
}

String urlDecode(const String& text)
{
    String decoded = "";
    char temp[] = "0x00";
    unsigned int len = text.length();
    unsigned int i = 0;
    while (i < len)
    {
        char decodedChar;
        char encodedChar = text.charAt(i++);
        if ((encodedChar == '%') && (i + 1 < len))
        {
            temp[2] = text.charAt(i++);
            temp[3] = text.charAt(i++);

            decodedChar = strtol(temp, NULL, 16);
        }
        else {
            if (encodedChar == '+')
            {
                decodedChar = ' ';
            }
            else {
                decodedChar = encodedChar;  // normal ascii char
            }
        }
        decoded += decodedChar;
    }
    return decoded;
}


void urlencode(String &input) { // LAL ^_^
  input.replace(String('\n'), "%0A");
  input.replace(" " , "+");
  input.replace("!" , "%21");
  input.replace("\"", "%22" );
  input.replace("#" , "%23");
  input.replace("$" , "%24");
  //input.replace("%" , "%25");
  input.replace("&" , "%26");
  input.replace("\'", "%27" );
  input.replace("(" , "%28");
  input.replace(")" , "%29");
  input.replace("*" , "%30");
  input.replace("+" , "%31");
  input.replace("," , "%2C");
  //input.replace("." , "%2E");
  input.replace("/" , "%2F");
  input.replace(":" , "%3A");
  input.replace(";" , "%3A");
  input.replace("<" , "%3C");
  input.replace("=" , "%3D");
  input.replace(">" , "%3E");
  input.replace("?" , "%3F");
  input.replace("@" , "%40");
  input.replace("[" , "%5B");
  input.replace("\\", "%5C" );
  input.replace("]" , "%5D");
  input.replace("^" , "%5E");
  input.replace("-" , "%5F");
  input.replace("`" , "%60");
  input.replace("{" , "%7B");
  input.replace("}" , "%7D");
}

String fileUrlEncode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;
    
}

String fileUrlEncode(char *array)
{
    String encodedString="";
    String str(array);
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;
    
}

