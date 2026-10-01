#ifndef MINIDISPLAY_STATUS_FORMAT_H
#define MINIDISPLAY_STATUS_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace minidisplay {

// "--" for missing values, otherwise a rounded whole percent such as "73%".
inline void formatPercent(int16_t tenths, char *output, size_t outputSize) {
  if (tenths < 0) {
    snprintf(output, outputSize, "--");
  } else {
    snprintf(output, outputSize, "%u%%", static_cast<unsigned>((tenths + 5) / 10));
  }
}

// Byte rates with one decimal and a single-letter binary unit: "338.9K/s".
inline void formatRate(uint32_t bytesPerSecond, char *output, size_t outputSize) {
  if (bytesPerSecond < 1024U) {
    snprintf(output, outputSize, "%luB/s", static_cast<unsigned long>(bytesPerSecond));
    return;
  }
  uint64_t divisor = 1024ULL;
  char unit = 'K';
  if (bytesPerSecond >= 1024UL * 1024UL * 1024UL) {
    divisor = 1024ULL * 1024ULL * 1024ULL;
    unit = 'G';
  } else if (bytesPerSecond >= 1024UL * 1024UL) {
    divisor = 1024ULL * 1024ULL;
    unit = 'M';
  }
  const uint64_t tenths =
      (static_cast<uint64_t>(bytesPerSecond) * 10ULL + divisor / 2ULL) / divisor;
  snprintf(output, outputSize, "%lu.%lu%c/s", static_cast<unsigned long>(tenths / 10ULL),
           static_cast<unsigned long>(tenths % 10ULL), unit);
}

// Splits the bridge's "CC" or "CC-REGION" label into the flag code and up to
// three characters of region detail. Missing labels become "--".
inline void splitNetworkLocation(const char *location, char country[3], char detail[4]) {
  strcpy(country, "--");
  strcpy(detail, "--");
  if (location == nullptr || strlen(location) < 2 || location[0] == '-') return;
  country[0] = location[0];
  country[1] = location[1];
  country[2] = '\0';
  const char *separator = strchr(location, '-');
  if (separator == nullptr || separator[1] == '\0') {
    strcpy(detail, country);
    return;
  }
  strncpy(detail, separator + 1, 3);
  detail[3] = '\0';
}

}  // namespace minidisplay

#endif
