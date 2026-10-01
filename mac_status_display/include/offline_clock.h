#ifndef MINIDISPLAY_OFFLINE_CLOCK_H
#define MINIDISPLAY_OFFLINE_CLOCK_H

#include "status_protocol.h"

namespace macstatus {

// $MSC2,<local wall-clock time encoded as a 10-digit UTC epoch>*CRC16
inline bool parseCalendarFrame(const char *line, uint32_t &epoch) {
  if (!validAuxFrame(line, "$MSC2,")) return false;
  if (verifiedFrameEnd(line) - line != 16) return false;
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

// $MSC1,<local seconds since midnight>*CRC16, independent of status frames.
inline bool parseClockFrame(const char *line, uint32_t &seconds) {
  if (!validAuxFrame(line, "$MSC1,")) return false;
  const char *star = verifiedFrameEnd(line);
  const size_t length = static_cast<size_t>(star - (line + 6));
  if (length == 0 || length > 5) return false;
  char value[6] = {};
  for (size_t i = 0; i < length; ++i) {
    if (line[6 + i] < '0' || line[6 + i] > '9') return false;
    value[i] = line[6 + i];
  }
  return parseUnsigned(value, 86399, seconds);
}

struct CivilDate {
  uint16_t year;
  uint8_t month;    // 1-12
  uint8_t day;      // 1-31
  uint8_t weekday;  // 0 = Sunday
};

// Proleptic Gregorian date for a day count since 1970-01-01 (H. Hinnant's
// days_from_civil inverse), without pulling TimeLib into the status page.
inline CivilDate civilFromDays(uint32_t days) {
  const uint32_t shifted = days + 719468UL;
  const uint32_t era = shifted / 146097UL;
  const uint32_t dayOfEra = shifted - era * 146097UL;
  const uint32_t yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const uint32_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const uint32_t monthIndex = (5 * dayOfYear + 2) / 153;
  CivilDate date;
  date.day = static_cast<uint8_t>(dayOfYear - (153 * monthIndex + 2) / 5 + 1);
  date.month = static_cast<uint8_t>(monthIndex < 10 ? monthIndex + 3 : monthIndex - 9);
  date.year = static_cast<uint16_t>(yearOfEra + era * 400 + (date.month <= 2 ? 1 : 0));
  date.weekday = static_cast<uint8_t>((days + 4) % 7);  // 1970-01-01 was a Thursday.
  return date;
}

class OfflineClock {
 public:
  bool valid() const { return valid_; }
  uint32_t seconds() const { return seconds_; }
  bool dateValid() const { return days_ != 0; }
  uint32_t days() const { return days_; }
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
