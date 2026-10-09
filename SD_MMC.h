#pragma once
#include <FS.h>
#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_FREQ_PROBING 400
enum sdcard_type_t { CARD_NONE, CARD_MMC, CARD_SD, CARD_SDHC, CARD_UNKNOWN };
class SDMMCFS : public fs::FS { public: bool begin(const char * = "/sdcard", bool = false, bool = false, int = 0, uint8_t = 5); void end(); bool setPins(int, int, int, int = -1, int = -1, int = -1); sdcard_type_t cardType(); uint64_t cardSize(); uint64_t totalBytes(); uint64_t usedBytes(); };
extern SDMMCFS SD_MMC;
