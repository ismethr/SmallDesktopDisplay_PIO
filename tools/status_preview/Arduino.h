#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#define PROGMEM
#define OUTPUT 1
#define TFT_BL 5
inline uint8_t pgm_read_byte(const uint8_t *p) { return *p; }
inline uint32_t &previewMillis() { static uint32_t value = 0; return value; }
inline uint32_t millis() { return previewMillis(); }
inline void delay(int) {}
inline void pinMode(int, int) {}
inline void analogWriteRange(int) {}
inline void analogWrite(int, int) {}
struct PreviewSerial {
  std::string input;
  void begin(int) {}
  int available() { return static_cast<int>(input.size()); }
  int read() { char c = input.front(); input.erase(0, 1); return c; }
  void println(const char *) {}
};
static PreviewSerial Serial;
