#pragma once
#include <Arduino.h>
class Preferences { public:
  bool begin(const char *, bool = false); void end();
  size_t getString(const char *, char *, size_t); String getString(const char *, String = String());
  size_t putString(const char *, const char *); size_t putString(const char *, String);
  bool getBool(const char *, bool = false); size_t putBool(const char *, bool);
  uint8_t getUChar(const char *, uint8_t = 0); size_t putUChar(const char *, uint8_t);
  float getFloat(const char *, float = 0); size_t putFloat(const char *, float);
  int32_t getInt(const char *, int32_t = 0); size_t putInt(const char *, int32_t);
  uint32_t getUInt(const char *, uint32_t = 0); size_t putUInt(const char *, uint32_t);
  size_t getBytesLength(const char *); size_t getBytes(const char *, void *, size_t); size_t putBytes(const char *, const void *, size_t);
  bool isKey(const char *); bool remove(const char *); bool clear(); };
