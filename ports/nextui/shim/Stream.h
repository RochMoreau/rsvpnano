#pragma once
#include "Print.h"

class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    void setTimeout(unsigned long timeout) { timeout_ = timeout; }
    unsigned long getTimeout() const { return timeout_; }
    virtual size_t readBytes(uint8_t* buffer, size_t length) {
        size_t count = 0;
        while (count < length) {
            const int c = read();
            if (c < 0) break;
            buffer[count++] = static_cast<uint8_t>(c);
        }
        return count;
    }
    size_t readBytes(char* buffer, size_t length) { return readBytes(reinterpret_cast<uint8_t*>(buffer), length); }
    String readString() {
        String out;
        for (int c = read(); c >= 0; c = read()) out += static_cast<char>(c);
        return out;
    }

protected:
    unsigned long timeout_ = 1000;
};
