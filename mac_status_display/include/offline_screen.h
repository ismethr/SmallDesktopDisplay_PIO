#ifndef MINIDISPLAY_OFFLINE_SCREEN_H
#define MINIDISPLAY_OFFLINE_SCREEN_H

#include <TFT_eSPI.h>

#include "offline_clock.h"

class OfflineWeatherPage;

namespace minidisplay {

// The page shown after the USB stream stops: the weather clock's LineAtom time
// plus, on hardware, the cached weather, date and animation. Host previews
// draw a text-only stand-in because they do not decode JPEG assets.
class OfflineScreen {
 public:
  explicit OfflineScreen(TFT_eSPI &tft) : tft_(tft) {}

  void begin();
  // Forces a full repaint on the next draw(), e.g. when switching pages.
  void invalidate() { drawn_ = false; }
  void draw(const macstatus::OfflineClock &clock);

  // Accepts a verified $MSW1 weather snapshot. Always false on host builds.
  bool acceptWeather(const char *line);
  bool hasWeather() const;

 private:
  void drawDigits(uint32_t seconds, bool full);

  TFT_eSPI &tft_;
  OfflineWeatherPage *weather_ = nullptr;
  bool drawn_ = false;
  bool drawnValid_ = false;
  uint32_t drawnSecond_ = UINT32_MAX;
};

}  // namespace minidisplay

#endif
