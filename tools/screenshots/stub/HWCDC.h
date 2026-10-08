#pragma once
#include <Arduino.h>
class HWCDC : public Stream { public: void begin(unsigned long); operator bool() const; };
