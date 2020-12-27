#include "Config.h"
#include <LittleFS.h>
#include <WiFiUdp.h>
#include "MksHTTPServer.h"
#include "MksEEPROM.h"

const char STR_MIME_TEXT_HTML[] PROGMEM = "text/html";
const char STR_MIME_TEXT_PLAIN[] PROGMEM = "text/plain";
const char STR_MIME_APPLICATION_JSON[] PROGMEM = "application/json";
const char STR_JSON_ERR_500_NO_DATA_RECEIVED[] PROGMEM = "{\"err\":\"ERROR 500: NO DATA RECEIVED\"}";
const char STR_JSON_ERR_500_NO_FILENAME_PROVIDED[] PROGMEM = "{\"err\": \"ERROR 500: NO FILENAME PROVIDED\"}";
const char STR_JSON_ERR_500_IS_BUSY[] PROGMEM = "{\"err\":\"ERROR 500: IS BUSY\"}";
#ifdef SHOW_PASSWORDS
const char WIFI_CONFIG_HTML[] PROGMEM = "<html><head><meta http-equiv='Content-Type' content='text/html;'><title>MKS WIFI</title><style>body{background: #b5ff6a;}.config{margin: 150px auto;width: 600px;height: 600px;overflow: hidden;</style></head>" \
                                        "<body><div class='config'></caption><br /><h2>Update</h2>" \
                                        "<form method='POST' action='update_sketch' enctype='multipart/form-data'><table border='0'><tr><td>wifi firmware:</td><td><input type='file' name='update' ></td><td><input type='submit' value='update'></td></tr></form>" \
                                        "<form method='POST' action='update_fs' enctype='multipart/form-data'><tr><td>web view:</td><td><input type='file' name='update' ></td><td><input type='submit' value='update'></td></tr></table></form>" \
                                        "<br /><br /><h2>WIFI Configuration</h2><form method='GET' action='update_cfg'><caption><input type='radio' id='wifi_mode_sta' name='wifi_mode' value='wifi_mode_sta' /><label for='wifi_mode_sta'>STA</label><br />"\
                                        "<input type='radio' id='wifi_mode_ap' name='wifi_mode' value='wifi_mode_ap' /><label for='wifi_mode_ap'>AP</label><br /><br /><table border='0'><tr><td>" \
                                        "WIFI: </td><td><input type='text' id='hidden_ssid' name='ssid' /></td></tr><tr><td>KEY: </td><td><input type='" \
                                        "text" \
                                        "' id='password' name='password' />" \
                                        "</td></tr><tr><td colspan=2 align='right'> <input type='submit' value='config and reboot'></td></tr></table></form></div></body></html>";
#else
const char WIFI_CONFIG_HTML[] PROGMEM = "<html><head><meta http-equiv='Content-Type' content='text/html;'><title>MKS WIFI</title><style>body{background: #b5ff6a;}.config{margin: 150px auto;width: 600px;height: 600px;overflow: hidden;</style></head>" \
                                        "<body><div class='config'></caption><br /><h2>Update</h2>"\
                                        "<form method='POST' action='update_sketch' enctype='multipart/form-data'><table border='0'><tr><td>wifi firmware:</td><td><input type='file' name='update' ></td><td><input type='submit' value='update'></td></tr></form>" \
                                        "<form method='POST' action='update_fs' enctype='multipart/form-data'><tr><td>web view:</td><td><input type='file' name='update' ></td><td><input type='submit' value='update'></td></tr></table></form>" \
                                        "<br /><br /><h2>WIFI Configuration</h2><form method='GET' action='update_cfg'><caption><input type='radio' id='wifi_mode_sta' name='wifi_mode' value='wifi_mode_sta' /><label for='wifi_mode_sta'>STA</label><br />" \
                                        "<input type='radio' id='wifi_mode_ap' name='wifi_mode' value='wifi_mode_ap' /><label for='wifi_mode_ap'>AP</label><br /><br /><table border='0'><tr><td>" \
                                        "WIFI: </td><td><input type='text' id='hidden_ssid' name='ssid' /></td></tr><tr><td>KEY: </td><td><input type='" \
                                        "password" \
                                        "' id='password' name='password' />" \
                                        "</td></tr><tr><td colspan=2 align='right'> <input type='submit' value='config and reboot'></td></tr></table></form></div></body></html>";
#endif

MksHTTPServer WebServer;

UPDATE_RESULT MksHTTPServer::_update_result= UPDATE_NOT_DONE;
bool MksHTTPServer::_transfer_file_flag= false;
ESP8266WebServer MksHTTPServer::_server;

MksHTTPServer::MksHTTPServer()
{
}

void MksHTTPServer::handle()
{
    _server.handleClient();
}

void MksHTTPServer::begin()
{
    _server.onNotFound(handleNotFound);
    //upload on root are ignored
    _server.on("/", HTTP_ANY, handleRoot);
    //update
    _server.on("/update_sketch", HTTP_ANY, handleUpdate, handleFileUpdate);
    _server.on("/update_fs", HTTP_ANY, handleUpdate, handleFileUpdate);
    _server.on("/update_web", HTTP_ANY, handleUpdate, handleFileUpdate);
    _server.on("/update_cfg", HTTP_GET, handleCfg);
    //upload
    _server.on("/upload", HTTP_ANY, handleUpload, handleFileUpload);
    _server.begin(80);
}

void MksHTTPServer::handleRoot()
{
    _server.send(200, FPSTR(STR_MIME_TEXT_HTML), WIFI_CONFIG_HTML);
}

void MksHTTPServer::handleNotFound()
{
    String path = _server.urlDecode(_server.uri());
    String contentType =  getContentType(path.c_str());
    String pathWithGz = path + ".gz";
    if(LittleFS.exists(pathWithGz.c_str()) || LittleFS.exists(path.c_str())) {
        log_mkswifi("Path found `%s`", path.c_str());
        if(LittleFS.exists(pathWithGz.c_str())) {
            _server.sendHeader("Content-Encoding", "gzip");
            path = pathWithGz;
            log_mkswifi("Path is gz `%s`", path.c_str());
        }
        File dataFile = LittleFS.open(path,"r");
        if (!dataFile) {
            _server.send(404, FPSTR(STR_MIME_APPLICATION_JSON), "{\"err\": \"404: " + _server.urlDecode(_server.uri()) + " NOT FOUND\"}");
            return;
        }
        _server.streamFile(dataFile, contentType);
        dataFile.close();
        return;
    }
    _server.send(404, FPSTR(STR_MIME_APPLICATION_JSON), "{\"err\": \"404: " + _server.urlDecode(_server.uri()) + " NOT FOUND\"}");
}

void MksHTTPServer::handleUpload()
{


}

void MksHTTPServer::handleFileUpload()
{

}

void MksHTTPServer::handleUpdate()
{
    if (_update_result == UPDATE_NOT_DONE) {
        _server.send(200, FPSTR(STR_MIME_TEXT_HTML), WIFI_CONFIG_HTML);
        return;
    } else if(_update_result == UPDATE_UNKNOW_ERROR) {
        _server.send(200, FPSTR(STR_MIME_TEXT_HTML), F("Update failed!") );

    } else if(_update_result == UPDATE_FILE_ERROR) {
        _server.send(200, FPSTR(STR_MIME_TEXT_HTML), F("File error"));

    } else {
        _server.send(200, FPSTR(STR_MIME_TEXT_HTML), Update.hasError() ? F("Update failed!") : F("<META http-equiv=\"refresh\" content=\"15;URL=\">Update successfully! Rebooting..."));

    }
    delay(2000);
    ESP.restart();
}
void MksHTTPServer::handleFileUpdate()
{
    // handler for the file upload, get's the sketch bytes, and writes
    // them through the Update object
    HTTPUpload& upload = _server.upload();
    if(upload.status == UPLOAD_FILE_START) {
        if(!((upload.filename=="MksWifi.bin") || (upload.filename=="MksWifi_Web.bin") || (upload.filename=="MksWifi_WebView.bin"))) {
            _update_result = UPDATE_FILE_ERROR;
            upload.status = UPLOAD_FILE_ABORTED;

            return;
        }

        WiFiUDP::stopAll();
        log_mkswifi("Update: %s\n", upload.filename.c_str());

        uint32_t maxSketchSpace;

        String uri = _server.uri();
        log_mkswifi("Uri: %s\n", uri.c_str());

        bool res = false;

        if(uri.startsWith("/update_sketch") ) {

            if(upload.filename.startsWith("MksWifi.bin") || upload.filename.startsWith("MksWifi_Web.bin")) {
                maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                log_mkswifi("maxSketchSpace: 0x%x\n", maxSketchSpace);
                res = Update.begin(maxSketchSpace);
            } else {
                _update_result = UPDATE_FILE_ERROR;
                upload.status = UPLOAD_FILE_ABORTED;
                return;
            }
        } else if(uri.startsWith("/update_fs") ||uri.startsWith("/update_web") ) {
            if(upload.filename.startsWith("MksWifi_WebView.bin")) {
                res = Update.begin(3 * 1024 * 1024, U_FS);
            } else {
                _update_result = UPDATE_FILE_ERROR;
                upload.status = UPLOAD_FILE_ABORTED;
                return;
            }
        } else {
            _update_result = UPDATE_COMM_ERROR;
            upload.status = UPLOAD_FILE_ABORTED;
            return;
        }

        _transfer_file_flag = true;

        log_mkswifi("Ready to write");

        if(!res) {
            //start with max available size
            log_mkswifi("Error starting update");
        }
    } else if(upload.status == UPLOAD_FILE_WRITE) {
        log_mkswifi(".");

        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            log_mkswifi("Size error");

        }
    } else if(upload.status == UPLOAD_FILE_END) {
        if(Update.end(true)) {
            //true to set the size to the current progress
            _update_result = UPDATE_SUCCESS;
            log_mkswifi("Update Success: %u\nRebooting...\n", upload.totalSize);
        }

    } else if( upload.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        log_mkswifi("Update was aborted");
    }
    delay(0);

}

/*
void handleUpload()
{
//Luc TODO

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
            //transfer_state = TRANSFER_READY;
            //digitalWrite(EspReqTransferPin, LOW);
        }
        else
        {
            transfer_file_flag = false;
        }
        //wait m3 reply for first frame
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



}*/


//helper to extract content type from file extension
//Check what is the content tye according extension file
const char* MksHTTPServer::getContentType (const char* filename)
{
    String file_name = filename;
    file_name.toLowerCase();
    if (file_name.endsWith (".htm") ) {
        return "text/html";
    } else if (file_name.endsWith (".html") ) {
        return "text/html";
    } else if (file_name.endsWith (".css") ) {
        return "text/css";
    } else if (file_name.endsWith (".js") ) {
        return "application/javascript";
    } else if (file_name.endsWith (".png") ) {
        return "image/png";
    } else if (file_name.endsWith (".gif") ) {
        return "image/gif";
    } else if (file_name.endsWith (".jpeg") ) {
        return "image/jpeg";
    } else if (file_name.endsWith (".jpg") ) {
        return "image/jpeg";
    } else if (file_name.endsWith (".ico") ) {
        return "image/x-icon";
    } else if (file_name.endsWith (".xml") ) {
        return "text/xml";
    } else if (file_name.endsWith (".pdf") ) {
        return "application/x-pdf";
    } else if (file_name.endsWith (".zip") ) {
        return "application/x-zip";
    } else if (file_name.endsWith (".gz") ) {
        return "application/x-gzip";
    } else if (file_name.endsWith (".txt") ) {
        return "text/plain";
    }
    return "application/octet-stream";
}




void MksHTTPServer::handleCfg()
{
    if ( _server.args() <= 0) {
        _server.send(500, FPSTR(STR_MIME_TEXT_PLAIN), F("Got no data, go back and retry"));
        return;
    }
    if ( _server.arg("ssid").length()==0) {
        _server.send(200, FPSTR(STR_MIME_TEXT_HTML), F("<p>wifi parameters error!</p>"));
    }
    if ((_server.arg("wifi_mode")=="wifi_mode_ap") && (_server.arg("password").length()>0) &&  (_server.arg("password").length()<8) ) {
        _server.send(500, FPSTR(STR_MIME_TEXT_PLAIN), F("wifi password length is not correct, go back and retry"));
        return;
    }

    EEPROM_WriteString(BAK_ADDRESS_WIFI_SSID, _server.arg("ssid").c_str(), 32);
    EEPROM_WriteString (BAK_ADDRESS_WIFI_KEY, _server.arg("password").c_str(), 64);
    EEPROM_WriteString (BAK_ADDRESS_WIFI_MODE, _server.arg("wifi_mode").c_str(), 32);
    EEPROM.put(BAK_ADDRESS_WIFI_VALID, 0x0a);
    EEPROM.put(BAK_ADDRESS_MANUAL_IP_FLAG, 0xff);
    EEPROM.commit();
    _server.send(200, FPSTR(STR_MIME_TEXT_HTML), F("<p>Configure successfully!<br />Please use the new ip to connect again.</p>"));
    delay(300);
    ESP.restart();

}
