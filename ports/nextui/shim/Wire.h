#pragma once
#include <Arduino.h>
// No I2C peripherals exist on the host: every transaction fails cleanly.
class TwoWire : public Stream {
public:
    explicit TwoWire(uint8_t bus = 0) : bus_(bus) {}
    bool begin(int = -1, int = -1, uint32_t = 0) { return false; }
    bool end() { return true; }
    bool setClock(uint32_t) { return true; }
    void setTimeOut(uint16_t) {}
    void setTimeout(uint16_t) {}
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 2; }
    size_t requestFrom(uint8_t, size_t, bool = true) { return 0; }
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }
    using Print::write;
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

private:
    uint8_t bus_;
};
extern TwoWire Wire;
extern TwoWire Wire1;
