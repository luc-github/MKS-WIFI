#include "Config.h"
#include "MksFiFoFile.h"
#define BUF_INC_POINTER(p)  ((p + 1 == FILE_FIFO_SIZE) ? 0:(p + 1))

FILE_FIFO gFileFifo;

FILE_FIFO::FILE_FIFO(){
   reset(); 
}

int FILE_FIFO::push(char *buf, int len)
    {
        int i = 0;
        while(i < len ) {
            if(_rP != BUF_INC_POINTER(_wP)) {
                _fifo[_wP] = *(buf + i) ;
                _wP = BUF_INC_POINTER(_wP);
                i++;
            } else {
                break;
            }
        }
        return i;
    }

int FILE_FIFO::pop(char * buf, int len)
    {
        int i = 0;

        while(i < len) {
            if(_rP != _wP) {
                buf[i] = _fifo[_rP];
                _rP= BUF_INC_POINTER(_rP);
                i++;
            } else {
                break;
            }
        }
        return i;

    }

void FILE_FIFO::reset()
    {
        _wP = 0;
        _rP = 0;
        memset(_fifo, 0, FILE_FIFO_SIZE);
    }

uint32_t FILE_FIFO::left()
    {
        if(_rP >  _wP) {
            return _rP - _wP - 1;
        } else {
            return FILE_FIFO_SIZE + _rP - _wP - 1;
        }

    }

bool FILE_FIFO::is_empty()
    {
        if(_rP == _wP) {
            return true;
        } else {
            return false;
        }
    }
