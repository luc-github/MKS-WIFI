/*
  defines.h - define declarations
*/

#pragma once

//Version
#define FW_VERSION "C1.0.4_201109_beta"

//Debug output
#define DEBUG_OUTPUT_SERIAL0    0
#define DEBUG_OUTPUT_TELNET     1

//Boards
#define TFT28       0
#define TFT24       1
#define ROBIN       2

typedef enum  {
    Unknown = 0,
    Client = 1,
    AccessPoint = 2
} OperatingState;

typedef enum {
    TRANSFER_IDLE,
    TRANSFER_BEGIN,
    TRANSFER_GET_FILE,
    TRANSFER_READY,
    TRANSFER_FRAGMENT

} TRANS_STATE;

