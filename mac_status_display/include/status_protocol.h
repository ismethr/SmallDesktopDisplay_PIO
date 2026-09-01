#ifndef MAC_STATUS_PROTOCOL_H
#define MAC_STATUS_PROTOCOL_H

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace macstatus {

constexpr int16_t kMissingCodexUsage = -1;
constexpr int16_t kMissingTemperature = -1;
constexpr size_t kMaximumFrameLength = 128;

struct StatusFrame {
  uint16_t sequence;
  uint16_t cpuTenths;
  uint16_t memoryTenths;
  int16_t cpuTemperatureTenths;
  int16_t gpuTemperatureTenths;
  int16_t codexRemainingTenths;
  bool codexUsageStale;
  uint32_t downloadBytesPerSecond;
  uint32_t uploadBytesPerSecond;
  char networkLocation[9];
  bool networkLocationStale;
  uint8_t brightnessPercent;
  uint8_t offlineBrightnessPercent;
};

inline uint16_t crc16Ccitt(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

inline int hexNibble(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

inline bool parseUnsigned(const char *text, uint32_t maximum, uint32_t &result) {
  if (text == nullptr || *text == '\0' || *text == '-') return false;
  errno = 0;
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0' || parsed > maximum) return false;
  result = static_cast<uint32_t>(parsed);
  return true;
}

inline bool parseSigned(const char *text, int32_t minimum, int32_t maximum, int32_t &result) {
  if (text == nullptr || *text == '\0') return false;
  errno = 0;
  char *end = nullptr;
  const long parsed = strtol(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
    return false;
  }
  result = static_cast<int32_t>(parsed);
  return true;
}

// Parses a mutable line in the form:
// Current protocol:
// $MSD4,seq,cpu10,mem10,cpu_temp10,gpu_temp10,codex_remaining10,codex_stale,down_bps,up_bps,network_location,location_stale,brightness,offline_brightness*CRC16
// MSD3 is also accepted during coordinated bridge/firmware upgrades and maps
// both temperatures to kMissingTemperature.
inline bool parseStatusFrame(char *line, StatusFrame &output) {
  if (line == nullptr || line[0] != '$') return false;
  char *star = strrchr(line, '*');
  if (star == nullptr || strlen(star + 1) != 4) return false;

  uint16_t receivedCrc = 0;
  for (uint8_t index = 0; index < 4; ++index) {
    const int nibble = hexNibble(star[index + 1]);
    if (nibble < 0) return false;
    receivedCrc = static_cast<uint16_t>((receivedCrc << 4) | nibble);
  }
  const uint8_t *payload = reinterpret_cast<const uint8_t *>(line + 1);
  const size_t payloadLength = static_cast<size_t>(star - (line + 1));
  if (crc16Ccitt(payload, payloadLength) != receivedCrc) return false;

  *star = '\0';
  char *save = nullptr;
  char *tokens[14] = {};
  size_t tokenCount = 0;
  for (char *token = strtok_r(line + 1, ",", &save); token != nullptr;
       token = strtok_r(nullptr, ",", &save)) {
    if (tokenCount >= 14) return false;
    tokens[tokenCount++] = token;
  }
  const bool isCurrent = tokenCount == 14 && strcmp(tokens[0], "MSD4") == 0;
  const bool isLegacy = tokenCount == 10 && strcmp(tokens[0], "MSD3") == 0;
  if (!isCurrent && !isLegacy) return false;

  const size_t codexIndex = isCurrent ? 6 : 4;
  const size_t staleIndex = isCurrent ? 7 : 5;
  const size_t downloadIndex = isCurrent ? 8 : 6;
  const size_t uploadIndex = isCurrent ? 9 : 7;
  const size_t brightnessIndex = isCurrent ? 12 : 8;
  const size_t offlineBrightnessIndex = isCurrent ? 13 : 9;

  uint32_t sequence = 0;
  uint32_t cpu = 0;
  uint32_t memory = 0;
  uint32_t codexStale = 0;
  uint32_t download = 0;
  uint32_t upload = 0;
  uint32_t locationStale = 0;
  uint32_t brightness = 0;
  uint32_t offlineBrightness = 0;
  int32_t cpuTemperature = kMissingTemperature;
  int32_t gpuTemperature = kMissingTemperature;
  int32_t codexRemaining = 0;
  if (!parseUnsigned(tokens[1], UINT16_MAX, sequence) ||
      !parseUnsigned(tokens[2], 1000, cpu) ||
      !parseUnsigned(tokens[3], 1000, memory) ||
      (isCurrent &&
       (!parseSigned(tokens[4], kMissingTemperature, 1500, cpuTemperature) ||
        !parseSigned(tokens[5], kMissingTemperature, 1500, gpuTemperature))) ||
      !parseSigned(tokens[codexIndex], kMissingCodexUsage, 1000, codexRemaining) ||
      !parseUnsigned(tokens[staleIndex], 1, codexStale) ||
      !parseUnsigned(tokens[downloadIndex], UINT32_MAX, download) ||
      !parseUnsigned(tokens[uploadIndex], UINT32_MAX, upload) ||
      (isCurrent &&
       (!parseUnsigned(tokens[11], 1, locationStale) || strlen(tokens[10]) < 2 ||
        strlen(tokens[10]) > 8)) ||
      !parseUnsigned(tokens[brightnessIndex], 100, brightness) ||
      !parseUnsigned(tokens[offlineBrightnessIndex], 100, offlineBrightness)) {
    return false;
  }

  if (isCurrent) {
    for (const char *cursor = tokens[10]; *cursor != '\0'; ++cursor) {
      const bool allowed = (*cursor >= 'A' && *cursor <= 'Z') ||
                           (*cursor >= '0' && *cursor <= '9') || *cursor == '-' ||
                           *cursor == '?';
      if (!allowed) return false;
    }
  }

  StatusFrame candidate;
  memset(&candidate, 0, sizeof(candidate));
  candidate.sequence = static_cast<uint16_t>(sequence);
  candidate.cpuTenths = static_cast<uint16_t>(cpu);
  candidate.memoryTenths = static_cast<uint16_t>(memory);
  candidate.cpuTemperatureTenths = static_cast<int16_t>(cpuTemperature);
  candidate.gpuTemperatureTenths = static_cast<int16_t>(gpuTemperature);
  candidate.codexRemainingTenths = static_cast<int16_t>(codexRemaining);
  candidate.codexUsageStale = codexStale != 0;
  candidate.downloadBytesPerSecond = download;
  candidate.uploadBytesPerSecond = upload;
  strncpy(candidate.networkLocation, isCurrent ? tokens[10] : "--",
          sizeof(candidate.networkLocation) - 1);
  candidate.networkLocationStale = isCurrent && locationStale != 0;
  candidate.brightnessPercent = static_cast<uint8_t>(brightness);
  candidate.offlineBrightnessPercent = static_cast<uint8_t>(offlineBrightness);
  output = candidate;
  return true;
}

}  // namespace macstatus

#endif
