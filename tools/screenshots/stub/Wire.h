#pragma once
#include <Arduino.h>
class TwoWire : public Stream { public: bool begin(int = -1, int = -1, uint32_t = 0); void beginTransmission(uint8_t); uint8_t endTransmission(bool = true); uint8_t requestFrom(uint8_t, uint8_t); void setClock(uint32_t); void setTimeOut(uint16_t); };
extern TwoWire Wire;
