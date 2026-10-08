#ifndef MINIDISPLAY_APP_H
#define MINIDISPLAY_APP_H

#include <TFT_eSPI.h>

#include "ai_usage_protocol.h"
#include "offline_clock.h"
#include "offline_screen.h"
#include "status_protocol.h"
#include "status_screen.h"

namespace minidisplay {

// Owns the display state machine: routes verified USB frames to the live
// status page, falls back to the offline clock when system frames stop, and
// drives the backlight. Timers are wrap-safe uint32_t millisecond counters.
class MiniDisplayApp {
 public:
  static constexpr uint32_t kOfflineAfterMs = 4000;
  // Quota frames (MSA1/MSA2) are cleared when the bridge stops sending them.
  static constexpr uint32_t kQuotaExpiryMs = 15000;
  static constexpr uint32_t kClaudeExpiryMs = kQuotaExpiryMs;
  static constexpr uint8_t kDefaultDayBrightness = 50;
  static constexpr uint8_t kDefaultOfflineBrightness = 5;

  explicit MiniDisplayApp(TFT_eSPI &tft) : tft_(tft), status_(tft), offlinePage_(tft) {}

  void begin(uint32_t now);
  // Handles one complete line without its terminator; the buffer may be modified.
  void handleLine(char *line, uint32_t now);
  void tick(uint32_t now);

  bool offline() const { return offline_; }
  uint8_t brightness() const { return brightness_; }
  const macstatus::QuotaFrame &claudeUsage() const { return claude_; }
  // What the Codex card shows: MSA2 when fresh, else MSD4's weekly value.
  macstatus::QuotaFrame codexUsage() const;
  const macstatus::OfflineClock &clock() const { return clock_; }

 private:
  void showStatus(const macstatus::StatusFrame &frame);
  void applyBrightness(uint8_t percent);

  TFT_eSPI &tft_;
  StatusScreen status_;
  OfflineScreen offlinePage_;
  macstatus::OfflineClock clock_;
  macstatus::QuotaFrame claude_;
  bool hasClaude_ = false;
  uint32_t lastClaudeAt_ = 0;
  macstatus::QuotaFrame codex_;
  bool hasCodex_ = false;
  uint32_t lastCodexAt_ = 0;
  // Codex weekly quota from the last MSD4 frame, for bridges without MSA2.
  int16_t statusCodexWeekTenths_ = macstatus::kMissingCodexUsage;
  bool statusCodexStale_ = false;
  bool offline_ = false;
  uint32_t lastStatusAt_ = 0;
  uint8_t brightness_ = UINT8_MAX;  // Unknown until the first write.
  uint8_t offlineBrightness_ = kDefaultOfflineBrightness;
};

}  // namespace minidisplay

#endif
