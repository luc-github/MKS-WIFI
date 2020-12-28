#ifndef _GCODE_H_
#define _GCODE_H_
#include <Arduino.h>


typedef enum {
    PRINTER_NOT_CONNECT,
    PRINTER_IDLE,
    PRINTER_PRINTING,
    PRINTER_PAUSE,
} PRINT_STATE;


typedef struct {
    int print_rate;
    int print_hours;
    int print_mins;
    int print_seconds;
    String file_name;
    int file_size;
} PRINT_FILE_INF;

typedef struct {
    float curSprayerTemp[2];
    float curBedTemp;
    float desireSprayerTemp[2];
    float desireBedTemp;
    String sd_file_list;
    PRINT_STATE print_state;
    PRINT_FILE_INF print_file_inf;


} PRINT_INF;

extern char M3_TYPE;
extern boolean GET_VERSION_OK;

extern PRINT_INF gPrinterInf;
extern bool file_list_flag;
extern bool getting_file_flag;


#ifdef __cplusplus
extern "C" {
#endif

extern void paser_cmd(uint8_t *cmdRxBuf);

#ifdef __cplusplus
}
#endif
#endif

