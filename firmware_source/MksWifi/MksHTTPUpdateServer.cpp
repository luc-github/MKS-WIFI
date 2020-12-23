#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include "Config.h"

#include "MksHTTPUpdateServer.h"

const char* MksHTTPUpdateServer::_serverIndex =
R"(<html><body><form method='POST' action='update_web' enctype='multipart/form-data'>
                  <input type='file' name='update'>
                  <input type='submit' value='Update'>
               </form>
         </body></html>)";
const char* MksHTTPUpdateServer::_failedResponse = R"(Update failed!)";
const char* MksHTTPUpdateServer::_successResponse = "<META http-equiv=\"refresh\" content=\"15;URL=\">Update successfully! Rebooting...";

extern boolean transfer_file_flag;

MksHTTPUpdateServer::MksHTTPUpdateServer()
{
  _server = NULL;
  _username = NULL;
  _password = NULL;
  _authenticated = false;
}

char update_wifi_firmware[] = "update_wifi_firmware";

typedef enum
{
    UPDATE_UNKNOW_ERROR,
    UPDATE_FILE_ERROR,
    UPDATE_COMM_ERROR,
    UPDATE_SUCCESS,
} UPDATE_RESULT;

UPDATE_RESULT Update_result = UPDATE_UNKNOW_ERROR;;
    
void MksHTTPUpdateServer::setup(ESP8266WebServer *server,  const char * username, const char * password)
{
    
    
    _server = server;
    _username = (char *)username;
    _password = (char *)password;
    // handler for the /update form page
    _server->on("/update_web", HTTP_GET, [&](){
    //  if(_username != NULL && _password != NULL && !_server->authenticate(_username, _password))
     //   return _server->requestAuthentication();
      _server->send(200, "text/html", _serverIndex);
    });

    // handler for the /update form POST (once file upload finishes)
    _server->on("/update_", HTTP_POST, [&](){
     /* if(!_authenticated)
        return _server->requestAuthentication();*/
        if(Update_result == UPDATE_UNKNOW_ERROR)
        {
             _server->send(200, "text/html", _failedResponse );
    
        }
    else if(Update_result == UPDATE_FILE_ERROR)
    {
             _server->send(200, "text/html", "File error");
        
        }
    else
    {
             _server->send(200, "text/html", Update.hasError() ? _failedResponse : _successResponse);
        
        }
    delay(2000);
        ESP.restart();
     
    
    },[&]()
        {
          // handler for the file upload, get's the sketch bytes, and writes
          // them through the Update object
          HTTPUpload& upload = _server->upload();
          if(upload.status == UPLOAD_FILE_START)
        {
            if((!upload.filename.startsWith("MksWifi.bin")) 
                && (!upload.filename.startsWith("MksWifi_Web.bin"))  
                && (!upload.filename.startsWith("MksWifi_WebView.bin")))
            {
                Update_result = UPDATE_FILE_ERROR;
                upload.status = UPLOAD_FILE_ABORTED;
            
                return;
            }

            WiFiUDP::stopAll();
            log_esp3d("Update: %s\n", upload.filename.c_str());
        
            uint32_t maxSketchSpace;    

            String uri = _server->uri();
            log_esp3d("Uri: %s\n", uri.c_str());

            bool res = false;
            
            if(uri.startsWith("/update_sketch") )
            {
                
                if(upload.filename.startsWith("MksWifi.bin") || upload.filename.startsWith("MksWifi_Web.bin"))
                {
                    maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                    log_esp3d("maxSketchSpace: 0x%x\n", maxSketchSpace);
                    res = Update.begin(maxSketchSpace);
                }
                else
                {
                    Update_result = UPDATE_FILE_ERROR;
                    upload.status = UPLOAD_FILE_ABORTED;
                    return;
                }
            }
            else if(uri.startsWith("/update_fs") ||uri.startsWith("/update_web") )
            {   
                if(upload.filename.startsWith("MksWifi_WebView.bin"))
                    res = Update.begin(3 * 1024 * 1024, U_FS);
                else
                {
                    Update_result = UPDATE_FILE_ERROR;
                    upload.status = UPLOAD_FILE_ABORTED;
                    
                    
                    return;
                }
            }
            else
            {
                Update_result = UPDATE_COMM_ERROR;
                upload.status = UPLOAD_FILE_ABORTED;
                return;
            }

            transfer_file_flag = true;

        //  Serial.printf("Ready to write\n");

            if(!res)
            {//start with max available size
                log_esp3d("Error starting update");
            }
            // _server->send(200, "text/html", "Please wait.  Updating......");
          } //else if(_authenticated && upload.status == UPLOAD_FILE_WRITE){
          else if(upload.status == UPLOAD_FILE_WRITE)
        {
            log_esp3d(".");
        
            if(Update.write(upload.buf, upload.currentSize) != upload.currentSize)
            {
                log_esp3d("Size error");

            }
          } //else if(_authenticated && upload.status == UPLOAD_FILE_END){
          else if(upload.status == UPLOAD_FILE_END)
        {
            if(Update.end(true))
            { //true to set the size to the current progress
                Update_result = UPDATE_SUCCESS;
                log_esp3d("Update Success: %u\nRebooting...\n", upload.totalSize);
            } 
            
        }// else if(_authenticated && upload.status == UPLOAD_FILE_ABORTED){
        else if( upload.status == UPLOAD_FILE_ABORTED)
        {
            Update.end();
            log_esp3d("Update was aborted");
        }
          delay(0);
        });
}
