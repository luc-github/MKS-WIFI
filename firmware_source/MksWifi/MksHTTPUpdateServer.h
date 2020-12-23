#ifndef __HTTP_UPDATE_SERVER_H
#define __HTTP_UPDATE_SERVER_H

#include <ESP8266WebServer.h>

class MksHTTPUpdateServer
{
  private:
    ESP8266WebServer *_server;
    static const char *_serverIndex;
    static const char *_failedResponse;
    static const char *_successResponse;
    char * _username;
    char * _password;
    bool _authenticated;
  public:
    MksHTTPUpdateServer();
    void setup(ESP8266WebServer *server)
    {
      setup(server, NULL, NULL);
    }
    void setup(ESP8266WebServer *server,  const char * username, const char * password);
};


#endif
