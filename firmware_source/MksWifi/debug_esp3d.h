/*
  debug_esp3d.h - esp3d debug functions

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

#ifndef _DEBUG_ESP3D_H
#define _DEBUG_ESP3D_H

#include "Config.h"
#undef log_esp3d
#undef log_esp3dS
#if defined(ESP_DEBUG_FEATURE)
extern const char * pathToFileName(const char * path);

//Serial
#if ESP_DEBUG_FEATURE == DEBUG_OUTPUT_SERIAL0
#define DEBUG_OUTPUT_SERIAL Serial
#define log_esp3d(format, ...) DEBUG_OUTPUT_SERIAL.printf("[ESP3D][%s:%u] %s(): " format "\r\n", pathToFileName(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define log_esp3dS(format, ...) DEBUG_OUTPUT_SERIAL.printf(format "\r\n", ##__VA_ARGS__)
#endif //DEBUG_OUTPUT_SERIAL0

#if ESP_DEBUG_FEATURE == DEBUG_OUTPUT_TELNET
void TelnetDebug(const char* format, ...);
#define log_esp3d(format, ...)TelnetDebug("[ESP3D][%s:%u] %s(): " format "\r\n", pathToFileName(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define log_esp3dS(format, ...) TelnetDebug(format "\r\n", ##__VA_ARGS__)
#endif //DEBUG_OUTPUT_TELNET 
#else
#define log_esp3d(format, ...)
#define log_esp3dS(format, ...) 
#endif //ESP_DEBUG_FEATURE
#endif //_DEBUG_ESP3D_H 
