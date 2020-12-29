#pragma once
#include <Arduino.h>
#define FILE_FIFO_SIZE  (4096)

class FILE_FIFO
{
public:
    FILE_FIFO();
    int push(char *buf, int len);
    int pop(char * buf, int len);
    void reset();
    uint32_t left();
    bool is_empty();

private:
    char _fifo[FILE_FIFO_SIZE];
    uint32_t _wP;
    uint32_t _rP;

};

extern FILE_FIFO gFileFifo;
