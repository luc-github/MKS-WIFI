#pragma once

class MksTCPServer
{
public:
    MksTCPServer();
    void begin();
    void handle();
    uint32_t  write(const uint8_t *sbuf, uint32_t len);
    WiFiClient currentClient()
    {
        return _currentClient;
    }
private:
    WiFiClient _currentClient;
    WiFiServer _server;
};

extern MksTCPServer TcpServer;
