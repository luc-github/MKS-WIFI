#include "Config.h"
#include <ESP8266WiFi.h>
#include "MksTCPServer.h"

#define TCP_SERVER_PORT 8080

MksTCPServer TcpServer;

MksTCPServer::MksTCPServer():_server(TCP_SERVER_PORT) {}

void MksTCPServer::begin()
{
    _server.begin();
}

uint32_t  MksTCPServer::write(const uint8_t *sbuf, uint32_t len)
{
    if (_currentClient && _currentClient.connected()) {
        return _currentClient.write(sbuf, len);
    }
    return 0;
}

void MksTCPServer::handle()
{
    if (_server.hasClient()) {
        //find free/disconnected spot
        if(_currentClient.connected()) {
            _currentClient.stop();
        }
        _currentClient = _server.available();

        if (_server.hasClient()) {
            //no free/disconnected spot so reject
            WiFiClient serverClient = _server.available();
            serverClient.stop();

        }
    }

    /*
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
                                       // transfer gcode
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
                                    //transfer_state = TRANSFER_READY;
                                    //digitalWrite(EspReqTransferPin, LOW);
                                    do_transfer();

                                    socket_busy_stamp = millis();
                                }


                            }
                        }

                    }
                }
            }

     */

}
