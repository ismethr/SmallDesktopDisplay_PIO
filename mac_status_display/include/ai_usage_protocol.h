#ifndef MINIDISPLAY_AI_USAGE_PROTOCOL_H
#define MINIDISPLAY_AI_USAGE_PROTOCOL_H

#include "status_protocol.h"

namespace macstatus {

// Remaining share of a subscription's 5-hour and weekly windows, in tenths of
// a percent; -1 marks a window the bridge could not read.
struct QuotaFrame {
  int16_t fiveHourTenths = -1;
  int16_t weekTenths = -1;
  bool stale = true;
  // Length of the long window in days; 0 when the bridge did not say (older
  // bridges, and Claude, whose long window is always a week).
  uint8_t windowDays = 0;
};

// Kept for code written before Codex gained its own 5-hour frame.
using ClaudeUsageFrame = QuotaFrame;

inline bool operator==(const QuotaFrame &left, const QuotaFrame &right) {
  return left.fiveHourTenths == right.fiveHourTenths && left.weekTenths == right.weekTenths &&
         left.stale == right.stale && left.windowDays == right.windowDays;
}

inline bool operator!=(const QuotaFrame &left, const QuotaFrame &right) {
  return !(left == right);
}

// $<prefix>five_hour_remaining10,week_remaining10,stale[,window_days]*CRC16,
// where prefix is a six-character "$MSAn," tag and the optional last field is
// the long window's length in days (0-99). Auxiliary frame: older receivers
// ignore it and it never keeps CPU data live.
inline bool parseQuotaFrame(const char *line, const char *prefix, QuotaFrame &output) {
  if (!validAuxFrame(line, prefix)) return false;
  const char *star = verifiedFrameEnd(line);
  const char *cursor = line + strlen(prefix);
  int32_t values[4] = {};
  size_t fields = 0;
  for (size_t index = 0; index < 4; ++index) {
    char token[6] = {};
    size_t length = 0;
    while (cursor < star && *cursor != ',') {
      if (length >= sizeof(token) - 1) return false;
      if ((*cursor < '0' || *cursor > '9') && !(length == 0 && *cursor == '-')) return false;
      token[length++] = *cursor++;
    }
    const int32_t minimum = index < 2 ? -1 : 0;
    const int32_t maximum = index < 2 ? 1000 : index == 2 ? 1 : 99;
    if (!parseSigned(token, minimum, maximum, values[index])) return false;
    fields = index + 1;
    if (cursor == star) break;
    if (index == 3 || *cursor++ != ',') return false;
  }
  if (fields < 3) return false;
  output.fiveHourTenths = static_cast<int16_t>(values[0]);
  output.weekTenths = static_cast<int16_t>(values[1]);
  output.stale = values[2] != 0;
  output.windowDays = static_cast<uint8_t>(values[3]);
  return true;
}

// $MSA1: Claude account quota.
inline bool parseClaudeUsageFrame(const char *line, QuotaFrame &output) {
  return parseQuotaFrame(line, "$MSA1,", output);
}

// $MSA2: Codex quota. Without it, the Codex weekly value still arrives in MSD4.
inline bool parseCodexUsageFrame(const char *line, QuotaFrame &output) {
  return parseQuotaFrame(line, "$MSA2,", output);
}

}  // namespace macstatus

#endif
