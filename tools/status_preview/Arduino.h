#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#define PROGMEM
#define OUTPUT 1
#define TFT_BL 5
inline uint8_t pgm_read_byte(const uint8_t *p) { return *p; }
inline void pinMode(int, int) {}
inline void analogWriteRange(int) {}
inline void analogWrite(int, int) {}
