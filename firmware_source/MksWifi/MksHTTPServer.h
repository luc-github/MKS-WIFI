#pragma once

#include <ESP8266WebServer.h>

typedef enum {
    UPDATE_NOT_DONE,
    UPDATE_UNKNOW_ERROR,
    UPDATE_FILE_ERROR,
    UPDATE_COMM_ERROR,
    UPDATE_SUCCESS,
} UPDATE_RESULT;

class MksHTTPServer
{
public:
    MksHTTPServer();
    void begin();
    void handle();
private:
    static UPDATE_RESULT _update_result;
    static bool _transfer_file_flag;
    static void handleNotFound ();
    static ESP8266WebServer _server;
    static void handleRoot ();
    static void handleUpload();
    static void handleFileUpload();
    static void handleUpdate();
    static void handleFileUpdate();
    static void handleCfg();
    static const char* getContentType (const char* filename);
};

extern MksHTTPServer WebServer;
