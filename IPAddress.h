#pragma once
#include <Arduino.h>
class IPAddress { public: String toString() const; operator uint32_t() const; };
