#pragma once
#include <Arduino.h>
class SPIClass { public: SPIClass(int = 0); void begin(int = -1, int = -1, int = -1, int = -1); void end(); };
extern SPIClass SPI;
#define FSPI 0
#define HSPI 1
