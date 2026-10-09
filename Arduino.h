#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <string>
#include <strings.h>
typedef uint8_t byte;
size_t strlcpy(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
class String {
 public:
  String(const char *s = "");
  String(int);
  String(unsigned);
  String(long);
  String(unsigned long);
  String(float, int = 2);
  String(double, int = 2);
  const char *c_str() const;
  unsigned length() const;
  String substring(unsigned, unsigned = 0) const;
  int lastIndexOf(char) const;
  int indexOf(const char *) const;
  int indexOf(char) const;
  bool endsWith(const char *) const;
  bool startsWith(const char *) const;
  bool operator==(const char *) const;
  bool operator!=(const char *) const;
  bool operator==(const String &) const;
  bool operator!=(const String &) const;
  String operator+(const String &) const;
  String operator+(const char *) const;
  String &operator+=(const String &);
  String &operator+=(const char *);
  String &operator+=(char);
  void reserve(unsigned);
  bool isEmpty() const;
  char operator[](unsigned) const;
  int toInt() const;
  void trim();
  void toLowerCase();
};
String operator+(const char *, const String &);
class Print {
 public:
  size_t printf(const char *, ...);
  size_t println(const char * = "");
  size_t println(const String &);
  size_t println(int);
  size_t print(const char *);
  size_t print(const String &);
  size_t print(int);
  size_t write(const uint8_t *, size_t);
  size_t write(uint8_t);
  void flush();
};
class Stream : public Print {
 public:
  int available();
  int read();
  size_t readBytes(char *, size_t);
};
unsigned long millis();
unsigned long micros();
void delay(unsigned long);
void yield();
long random(long);
long random(long, long);
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
#define MALLOC_CAP_DMA 8
void *heap_caps_malloc(size_t, uint32_t);
void *heap_caps_calloc(size_t, size_t, uint32_t);
size_t heap_caps_get_free_size(uint32_t);
size_t heap_caps_get_largest_free_block(uint32_t);
size_t heap_caps_get_minimum_free_size(uint32_t);
/* FreeRTOS */
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
#define portMAX_DELAY 0xffffffff
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateBinary();
int xSemaphoreTake(SemaphoreHandle_t, uint32_t);
int xSemaphoreGive(SemaphoreHandle_t);
void vTaskDelay(uint32_t);
void vTaskDelete(TaskHandle_t);
int xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *, int, TaskHandle_t *, int);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);
TaskHandle_t xTaskGetHandle(const char *);
TickType_t xTaskGetTickCount();
class EspClass {
 public:
  void restart();
  uint32_t getFreeHeap();
  uint32_t getCpuFreqMHz();
  uint32_t getFreePsram();
  uint32_t getPsramSize();
};
extern EspClass ESP;
void esp_deep_sleep_start();
bool setCpuFrequencyMhz(uint32_t);
uint32_t getCpuFrequencyMhz();
void configTime(long, int, const char *, const char * = 0, const char * = 0);
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
void pinMode(int, int);
void digitalWrite(int, int);
int analogRead(int);
#define FILE_WRITE "w"
#define FILE_READ "r"
#define FILE_APPEND "a"
uint32_t esp_random();
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
void delayMicroseconds(unsigned);
#define INPUT_PULLUP 2
