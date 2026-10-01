#ifndef MINIDISPLAY_STATUS_SCREEN_H
#define MINIDISPLAY_STATUS_SCREEN_H

#include <TFT_eSPI.h>

#include "ai_usage_protocol.h"
#include "offline_clock.h"
#include "status_protocol.h"

namespace minidisplay {

// The live 240 x 240 system page. It remembers what is on the panel and only
// repaints regions whose values changed, which keeps SPI traffic and flicker low.
class StatusScreen {
 public:
  explicit StatusScreen(TFT_eSPI &tft) : tft_(tft), shown_() {}

  // Clears the panel, draws the card chrome with "--" placeholders and a
  // WAITING indicator. The next show*() calls repaint every field.
  void drawLayout();

  void showFrame(const macstatus::StatusFrame &frame);
  void showClaude(const macstatus::ClaudeUsageFrame &usage);
  // Header shows local time and date once the bridge has synchronised them.
  void showClock(const macstatus::OfflineClock &clock);

 private:
  void drawHeader(const macstatus::OfflineClock *clock);
  void drawConnection(bool live);
  void drawLoad(int16_t x, const char *label, int16_t tenths);
  void drawTemperature(int16_t x, const char *label, int16_t tenths);
  void drawUsageCard(int16_t x, const char *label, int16_t remainingTenths, bool stale,
                     uint16_t accent);
  void drawCodex(int16_t remainingTenths, bool stale);
  void drawClaude(const macstatus::ClaudeUsageFrame &usage);
  void drawLocation(const char *location, bool stale);
  void drawRates(bool valid, uint32_t download, uint32_t upload);
  void drawProgressBar(int16_t x, int16_t y, int16_t width, int16_t height, int16_t tenths,
                       uint16_t color);
  void setText(uint8_t font, uint8_t datum, uint16_t color);

  static constexpr uint32_t kNoMinute = UINT32_MAX;

  TFT_eSPI &tft_;
  bool hasFrame_ = false;
  macstatus::StatusFrame shown_;
  bool claudeDrawn_ = false;
  macstatus::ClaudeUsageFrame claude_;
  uint32_t headerMinute_ = kNoMinute;
  bool headerDate_ = false;
};

}  // namespace minidisplay

#endif
