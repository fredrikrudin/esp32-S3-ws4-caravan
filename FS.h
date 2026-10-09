#pragma once
#include <Arduino.h>
namespace fs {
class File : public Stream {
 public:
  operator bool() const;
  size_t size();
  size_t read(uint8_t *, size_t);
  int read();
  void close();
  bool isDirectory();
  File openNextFile();
  const char *name() const;
  const char *path() const;
  bool seek(uint32_t);
  size_t position();
  String readStringUntil(char);
};
class FS {
 public:
  File open(const char *, const char * = FILE_READ, bool = false);
  File open(const String &, const char * = FILE_READ, bool = false);
  bool exists(const char *);
  bool remove(const char *);
  bool rename(const char *, const char *);
  bool mkdir(const char *);
};
}  // namespace fs
using fs::File;
using fs::FS;
