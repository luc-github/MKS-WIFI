#pragma once

#define UART_FRAME_SIZE 1024

class MksSerialCom
{
public:
    MksSerialCom();
    void begin();
    void handle();
    bool sendNetworkInfos();
    void NetworkInfosFragment(bool force = false);
    void staticIPInfosFragment();
    void gcodeFragment(const char *dataField, bool important);
    void fileNameFragment(const char *fileName, uint32_t postLength);
    void fileFragment(uint8_t *dataField, uint32_t fragLen, int32_t fragment);
    void hotSpotFragment();
    void communicationMode();
    void transferMode();
    bool transferFragment(bool isImportant=false);
private:
    bool canSendFrame();
    void clearFrame(bool isImportant = false);
    char _frame[UART_FRAME_SIZE];
    char _importantFrame[UART_FRAME_SIZE];
    uint16_t _frameSize;
    uint16_t _importantFrameSize;

};

extern MksSerialCom serialcom;
