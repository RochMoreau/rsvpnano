#pragma once
#include "Stream.h"

// Serial console: output goes to the log (stderr), input is never available.
class HardwareSerial : public Stream {
public:
    void begin(unsigned long, uint32_t = 0, int8_t = -1, int8_t = -1) {}
    void end() {}
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    using Print::write;
    void flush() override;
    void setDebugOutput(bool) {}
    size_t setRxBufferSize(size_t size) { return size; }
    size_t setTxBufferSize(size_t size) { return size; }
    void setTxTimeoutMs(uint32_t) {}
    explicit operator bool() const { return true; }
};

using HWCDC = HardwareSerial;
using USBCDC = HardwareSerial;
extern HardwareSerial Serial;
extern HardwareSerial Serial0;
