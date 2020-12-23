// Configuration for MksWiFi

#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

//Defines
#define DEBUG_OUTPUT_SERIAL0    0
#define DEBUG_OUTPUT_TELNET     1

//DEBUG
//#define ESP_DEBUG_FEATURE DEBUG_OUTPUT_TELNET

//HTML
#define SHOW_PASSWORDS

//Pins
const int EspReqTransferPin = 0;  // GPIO0, output, indicates to the printer board that we want to send something
const int McuTfrReadyPin = 4;     // GPIO4, input, indicates that printer board is ready get command

#endif



