/*
  MksDebug.cpp -  debug functions class

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with This code; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "Config.h"

#if defined(ESP_DEBUG_FEATURE)
#include <ESP8266WiFi.h>
const char * pathToFileName(const char * path)
{
    size_t i = 0;
    size_t pos = 0;
    char * p = (char *)path;
    while(*p) {
        i++;
        if(*p == '/' || *p == '\\') {
            pos = i;
        }
        p++;
    }
    return path+pos;
}

#if ESP_DEBUG_FEATURE == DEBUG_OUTPUT_TELNET
WiFiServer debugServer(8000);
WiFiClient  debugClient;

bool isDebugStarted = false;

void TelnetDebug(const char* format, ...){
    
    char    loc_buf[64];
    char*   temp = loc_buf;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    if(!isDebugStarted) {
        isDebugStarted=true;
        debugServer.begin();
        debugServer.setNoDelay(true);
    }
    size_t len = vsnprintf(NULL, 0, format, arg);
    va_end(copy);
    if (len >= sizeof(loc_buf)) {
        temp = new char[len + 1];
        if (temp == NULL) {
            return;
        }
    }
    len = vsnprintf(temp, len + 1, format, arg);
    if (debugServer.hasClient()) {
        if (!debugClient || !debugClient.connected()) {
            if(debugClient) {
                debugClient.stop();
            }
        }
        debugClient = debugServer.available();
    }
    if(debugClient){
        if (debugClient.availableForWrite()>=len){
            debugClient.write(temp, len);
        }
    }
    va_end(arg);
    if (temp != loc_buf) {
        delete[] temp;
    }
}

#endif //DEBUG_OUTPUT_TELNET 

#endif //ESP_DEBUG_FEATURE
