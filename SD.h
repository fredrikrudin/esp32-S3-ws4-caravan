#pragma once
#include <FS.h>
#include <SPI.h>
#include <SD_MMC.h>
class SDFS : public fs::FS { public: bool begin(uint8_t = 0, SPIClass & = SPI, uint32_t = 4000000, const char * = "/sd", uint8_t = 5, bool = false); void end(); sdcard_type_t cardType(); uint64_t cardSize(); uint64_t totalBytes(); uint64_t usedBytes(); };
extern SDFS SD;
