#ifndef MINIDISPLAY_OFFLINE_CLOCK_H
#define MINIDISPLAY_OFFLINE_CLOCK_H

#include "status_protocol.h"

namespace macstatus {

inline bool validAuxFrame(const char *line, const char *prefix) {
  if (!line || strncmp(line, prefix, strlen(prefix)) != 0) return false;
  const char *star = strrchr(line, '*');
  if (!star || strlen(star + 1) != 4 || star - line > 1000) return false;
  uint16_t crc = 0;
  for (int i = 1; i <= 4; ++i) {
    int digit = hexNibble(star[i]);
    if (digit < 0) return false;
    crc = static_cast<uint16_t>((crc << 4) | digit);
  }
  return crc == crc16Ccitt(reinterpret_cast<const uint8_t *>(line + 1), star - line - 1);
}

inline bool parseCalendarFrame(const char *line, uint32_t &epoch) {
  if (!validAuxFrame(line, "$MSC2,")) return false;
  const char *star = strchr(line, '*');
  if (star - line != 16) return false;
  char value[11] = {};
  for (int i = 0; i < 10; ++i) {
    if (line[6 + i] < '0' || line[6 + i] > '9') return false;
    value[i] = line[6 + i];
  }
  uint32_t parsed = 0;
  if (!parseUnsigned(value, 4102444799UL, parsed) || parsed < 1577836800UL) return false;
  epoch = parsed;
  return true;
}

// Local wall-clock seconds supplied by the host, independent of status frames.
inline bool parseClockFrame(const char *line, uint32_t &seconds) {
  if (line == nullptr || strncmp(line, "$MSC1,", 6) != 0) return false;
  const char *star = strchr(line, '*');
  if (star == nullptr || strlen(star + 1) != 4 || star - line > 11) return false;
  uint16_t crc = 0;
  for (size_t i = 1; i <= 4; ++i) {
    const int nibble = hexNibble(star[i]);
    if (nibble < 0) return false;
    crc = static_cast<uint16_t>((crc << 4) | nibble);
  }
  if (crc != crc16Ccitt(reinterpret_cast<const uint8_t *>(line + 1), star - line - 1)) return false;
  char value[6] = {};
  const size_t length = static_cast<size_t>(star - (line + 6));
  if (length == 0 || length > 5) return false;
  for (size_t i = 0; i < length; ++i) {
    if (line[6 + i] < '0' || line[6 + i] > '9') return false;
    value[i] = line[6 + i];
  }
  return parseUnsigned(value, 86399, seconds);
}

class OfflineClock {
 public:
  bool valid() const { return valid_; }
  uint32_t seconds() const { return seconds_; }
  bool dateValid() const { return days_ != 0; }
  uint32_t epoch() const { return days_ * 86400UL + seconds_; }
  void syncEpoch(uint32_t epoch, uint32_t now) {
    sync(epoch % 86400, now);
    days_ = epoch / 86400;
  }
  void sync(uint32_t seconds, uint32_t now) {
    if (seconds >= 86400) return;
    seconds_ = seconds;
    previous_ = now;
    remainder_ = 0;
    valid_ = true;
  }
  void tick(uint32_t now) {
    if (!valid_) return;
    const uint32_t elapsed = now - previous_;
    previous_ = now;
    seconds_ += elapsed / 1000;
    remainder_ += elapsed % 1000;
    seconds_ += remainder_ / 1000;
    if (days_) days_ += seconds_ / 86400;
    seconds_ %= 86400;
    remainder_ %= 1000;
  }
 private:
  bool valid_ = false;
  uint32_t seconds_ = 0, previous_ = 0, remainder_ = 0;
  uint32_t days_ = 0;
};

}  // namespace macstatus
#endif
