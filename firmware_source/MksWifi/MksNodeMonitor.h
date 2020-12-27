#pragma once
#include <WiFiUdp.h>
class MksNodeMonitor
{
  public:
    MksNodeMonitor();
    void begin();
    void handle();
  private:
    WiFiUDP _node_monitor;
    
};

extern MksNodeMonitor NodeMonitor;
