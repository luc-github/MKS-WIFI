#include "Config.h"
#include <ESP8266WiFi.h>
#include "MksNodeMonitor.h"

#define UDP_PORT    8989

MksNodeMonitor NodeMonitor;

extern OperatingState currentState;
extern char moduleId[21];

MksNodeMonitor::MksNodeMonitor()
{

}
void MksNodeMonitor::begin()
{
    log_mkswifi("Start Node Monitor Server");
    _node_monitor.begin(UDP_PORT);
}
void MksNodeMonitor::handle()
{
    char packetBuffer[200];
    int packetSize = _node_monitor.parsePacket();
    char  ReplyBuffer[50] = "mkswifi:";
    if (packetSize) {
        log_mkswifi("Got node monitor packet");
        // read the packet into packetBufffer
        _node_monitor.read(packetBuffer, sizeof(packetBuffer));
        log_mkswifi(packetBuffer);
        if(strstr(packetBuffer, "mkswifi")) {
            memcpy(&ReplyBuffer[strlen("mkswifi:")], moduleId, strlen(moduleId));
            ReplyBuffer[strlen("mkswifi:") + strlen(moduleId)] = ',';
            if(currentState == OperatingState::Client) {
                strcpy(&ReplyBuffer[strlen("mkswifi:") + strlen(moduleId) + 1], WiFi.localIP().toString().c_str());
                ReplyBuffer[strlen("mkswifi:") + strlen(moduleId) + strlen(WiFi.localIP().toString().c_str()) + 1] = '\n';
            } else if(currentState == OperatingState::AccessPoint) {
                strcpy(&ReplyBuffer[strlen("mkswifi:") + strlen(moduleId) + 1], WiFi.softAPIP().toString().c_str());
                ReplyBuffer[strlen("mkswifi:") + strlen(moduleId) + strlen(WiFi.softAPIP().toString().c_str()) + 1] = '\n';
            }
            // send a reply, to the IP address and port that sent us the packet we received
            _node_monitor.beginPacket(_node_monitor.remoteIP(), _node_monitor.remotePort());
            _node_monitor.write(ReplyBuffer, strlen(ReplyBuffer));
            _node_monitor.endPacket();
        }
    }

}
