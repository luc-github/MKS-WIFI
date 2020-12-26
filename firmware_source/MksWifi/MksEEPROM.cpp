#include "MksEEPROM.h"

bool EEPROM_WriteString (int pos, const char * byte_buffer, uint8_t max)
    {
        uint8_t size = strlen(byte_buffer);
        int i;
        if(size>max)size=max;
        if(pos + max > EEPROM_SIZE) return false;
        for (i = 0; i < size; i++) {
            EEPROM.write (pos + i, byte_buffer[i]);
        }
        if (i<max) EEPROM.write (pos + i, 0x00);

        return true;
    }

