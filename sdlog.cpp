/* Logging to the Serial Monitor, to a ring buffer in PSRAM (readable at /log in
   the browser) and, if switched on, to the TF card.
   The card uses SDMMC in 1-bit mode: GPIO2 clock, GPIO1 command, GPIO4 data,
   so no chip-select pin is needed. Those pins are shared with the display's
   configuration SPI, which is only used while the panel starts up. */
#include "app.h"
#include <SD_MMC.h>

#define SD_CLK 2  // also LCD_SCL: the panel only uses it while starting up
#define SD_CMD 1  // also LCD_SDA
#define SD_D0 4
#define LOG_RING 12288  // bytes kept for the web view (smaller = less PSRAM traffic)
#define SD_FLUSH_MS 5000  // how often the file is flushed

static SemaphoreHandle_t log_mtx;
static char *ring = nullptr;  // in PSRAM
static size_t ring_len = 0;   // bytes used (grows to LOG_RING, then wraps)
static size_t ring_pos = 0;
static bool ring_wrapped = false;

static bool sd_mounted = false;
static char sd_msg[64] = "Not started";
static File log_file;
static char log_name[32] = "/caravan.log";
static uint32_t last_flush = 0;
static uint32_t dropped = 0;

static void ring_write(const char *s, size_t n) {
  if (!ring) return;
  for (size_t i = 0; i < n; i++) {
    ring[ring_pos++] = s[i];
    if (ring_pos >= LOG_RING) {
      ring_pos = 0;
      ring_wrapped = true;
    }
  }
  ring_len = ring_wrapped ? LOG_RING : ring_pos;
}

/* Waveshare's own SD demo does exactly this: set the three pins, mount in
   1-bit mode. The CH32 chip is not involved, so nothing else is touched. */
bool sd_log_mount() {
  if (sd_mounted) return true;

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  bool ok = SD_MMC.begin("/sdcard", true);  // true = 1-bit mode
  if (!ok) {  // one retry: the card sometimes needs a moment after being inserted
    SD_MMC.end();
    delay(100);
    ok = SD_MMC.begin("/sdcard", true);
  }
  if (!ok || SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    strlcpy(sd_msg, "No card found", sizeof(sd_msg));
    logf("SD: no card found");
    return false;
  }

  uint64_t mb = SD_MMC.cardSize() / (1024 * 1024);
  log_file = SD_MMC.open(log_name, FILE_APPEND);
  if (!log_file) {
    SD_MMC.end();
    strlcpy(sd_msg, "Card found, but cannot write", sizeof(sd_msg));
    logf("SD: card found (%llu MB) but the file could not be opened - FAT32?", (unsigned long long)mb);
    return false;
  }
  sd_mounted = true;
  snprintf(sd_msg, sizeof(sd_msg), "Logging to %s (card %llu MB)", log_name, (unsigned long long)mb);
  logf("SD: mounted, card %llu MB", (unsigned long long)mb);
  log_file.printf("\n--- board started, %lu ms ---\n", (unsigned long)millis());
  return true;
}

fs::FS &sd_fs() {
  return SD_MMC;
}

void sd_log_unmount() {
  if (!sd_mounted) return;
  log_file.close();
  SD_MMC.end();
  sd_mounted = false;
  strlcpy(sd_msg, "Card not in use", sizeof(sd_msg));
}

bool sd_log_ok() {
  return sd_mounted;
}

const char *sd_log_status() {
  return sd_msg;
}

size_t sd_log_size() {
  return sd_mounted ? log_file.size() : 0;
}

void log_begin() {
  log_mtx = xSemaphoreCreateMutex();
  ring = (char *)heap_caps_malloc(LOG_RING, MALLOC_CAP_SPIRAM);
  if (!ring) USBSerial.println("Log ring buffer allocation failed");
}

void logf(const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
  if (n && buf[n - 1] != '\n') {  // every entry is one line
    buf[n++] = '\n';
    buf[n] = 0;
  }

  USBSerial.print(buf);
  if (!log_mtx) return;

  if (xSemaphoreTake(log_mtx, pdMS_TO_TICKS(50)) != pdTRUE) {
    dropped++;
    return;
  }
  ring_write(buf, n);
  if (feat_sdlog) {
    if (!sd_mounted) sd_log_mount();
    if (sd_mounted) {
      log_file.print(buf);
      if (millis() - last_flush > SD_FLUSH_MS) {
        log_file.flush();
        last_flush = millis();
      }
    }
  } else if (sd_mounted) {
    sd_log_unmount();
  }
  xSemaphoreGive(log_mtx);
}

/* Copies the ring buffer, oldest first, into the caller's String */
void log_dump(String &out) {
  if (!ring || !log_mtx) return;
  xSemaphoreTake(log_mtx, portMAX_DELAY);
  out.reserve(ring_len + 64);
  if (ring_wrapped) out.concat(ring + ring_pos, LOG_RING - ring_pos);
  out.concat(ring, ring_pos);
  xSemaphoreGive(log_mtx);
}

/* ---------- card management (all called from the UI) ---------- */
const char *sd_log_name() {
  return log_name;
}

/* Card type, size and how much is used */
void sd_card_info(char *out, size_t n) {
  if (!sd_mounted) {
    snprintf(out, n, "No card mounted");
    return;
  }
  const char *type = "unknown";
  switch (SD_MMC.cardType()) {
    case CARD_MMC: type = "MMC"; break;
    case CARD_SD: type = "SDSC"; break;
    case CARD_SDHC: type = "SDHC"; break;
    default: break;
  }
  uint64_t total = SD_MMC.totalBytes() / (1024 * 1024);
  uint64_t used = SD_MMC.usedBytes() / (1024 * 1024);
  snprintf(out, n, "%s card, %llu MB used of %llu MB", type, (unsigned long long)used, (unsigned long long)total);
}

/* Starts a new file: caravan.log, caravan-1.log, caravan-2.log ... */
bool sd_log_new_file() {
  if (!sd_mounted) return false;
  xSemaphoreTake(log_mtx, portMAX_DELAY);
  log_file.close();
  for (int i = 1; i < 100; i++) {
    char name[32];
    snprintf(name, sizeof(name), "/caravan-%d.log", i);
    if (!sd_fs().exists(name)) {
      strlcpy(log_name, name, sizeof(log_name));
      break;
    }
  }
  log_file = sd_fs().open(log_name, FILE_WRITE);
  bool ok = (bool)log_file;
  snprintf(sd_msg, sizeof(sd_msg), ok ? "Logging to %s" : "Could not create %s", log_name);
  xSemaphoreGive(log_mtx);
  return ok;
}

/* Deletes every log file except the one being written */
int sd_log_delete_old() {
  if (!sd_mounted) return 0;
  int n = 0;
  xSemaphoreTake(log_mtx, portMAX_DELAY);
  File root = sd_fs().open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = String("/") + f.name();
    bool is_log = name.endsWith(".log");
    f.close();
    if (is_log && name != String(log_name)) {
      if (sd_fs().remove(name.c_str())) n++;
    }
  }
  root.close();
  xSemaphoreGive(log_mtx);
  return n;
}

/* Files on the card, one per line, for the web page */
void sd_list_files(String &out) {
  if (!sd_mounted) {
    out = "No card mounted";
    return;
  }
  File root = sd_fs().open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    out += f.name();
    out += '\t';
    out += String((uint32_t)f.size());
    out += '\n';
    f.close();
  }
  root.close();
}
