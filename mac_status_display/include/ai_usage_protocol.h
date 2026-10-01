#ifndef MINIDISPLAY_AI_USAGE_PROTOCOL_H
#define MINIDISPLAY_AI_USAGE_PROTOCOL_H

#include "status_protocol.h"

namespace macstatus {

struct ClaudeUsageFrame {
  int16_t fiveHourTenths = -1;
  int16_t weekTenths = -1;
  bool stale = true;
};

inline bool operator==(const ClaudeUsageFrame &left, const ClaudeUsageFrame &right) {
  return left.fiveHourTenths == right.fiveHourTenths && left.weekTenths == right.weekTenths &&
         left.stale == right.stale;
}

inline bool operator!=(const ClaudeUsageFrame &left, const ClaudeUsageFrame &right) {
  return !(left == right);
}

// $MSA1,five_hour_remaining10,week_remaining10,stale*CRC16
// Auxiliary frame: older MSD4 receivers ignore it; it never keeps CPU data live.
inline bool parseClaudeUsageFrame(const char *line, ClaudeUsageFrame &output) {
  if (!validAuxFrame(line, "$MSA1,")) return false;
  const char *star = verifiedFrameEnd(line);
  const char *cursor = line + 6;
  int32_t values[3] = {};
  for (size_t index = 0; index < 3; ++index) {
    char token[6] = {};
    size_t length = 0;
    while (cursor < star && *cursor != ',') {
      if (length >= sizeof(token) - 1) return false;
      if ((*cursor < '0' || *cursor > '9') && !(length == 0 && *cursor == '-')) return false;
      token[length++] = *cursor++;
    }
    if (!parseSigned(token, index == 2 ? 0 : -1, index == 2 ? 1 : 1000, values[index])) return false;
    if (index < 2) {
      if (cursor == star || *cursor++ != ',') return false;
    } else if (cursor != star) return false;
  }
  output.fiveHourTenths = static_cast<int16_t>(values[0]);
  output.weekTenths = static_cast<int16_t>(values[1]);
  output.stale = values[2] != 0;
  return true;
}

}  // namespace macstatus

#endif
