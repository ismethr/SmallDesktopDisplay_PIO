#include "offline_screen.h"

#include "../../src/core/ClockFontRenderer.h"
#include "display_theme.h"

#ifdef ARDUINO
// The weather page pulls in large PROGMEM fonts: include it from this file only.
#include "offline_weather.h"
#endif

namespace minidisplay {

#ifdef ARDUINO
namespace {

TFT_eSPI *jpegTarget = nullptr;

bool pushJpegBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *pixels) {
  if (jpegTarget == nullptr || x < 0 || y < 0 || x + width > 240 || y + height > 240) return false;
  jpegTarget->pushImage(x, y, width, height, pixels);
  return true;
}

}  // namespace
#endif

void OfflineScreen::begin() {
#ifdef ARDUINO
  static OfflineWeatherPage page(tft_);
  weather_ = &page;
  jpegTarget = &tft_;
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(pushJpegBlock);
#endif
}

bool OfflineScreen::acceptWeather(const char *line) {
#ifdef ARDUINO
  return weather_ != nullptr && weather_->accept(line);
#else
  (void)line;
  return false;
#endif
}

bool OfflineScreen::hasWeather() const {
#ifdef ARDUINO
  return weather_ != nullptr && weather_->hasWeather();
#else
  return false;
#endif
}

void OfflineScreen::drawDigits(uint32_t seconds, bool full) {
  // Same cells, colors and sizes as the weather clock firmware.
  if (full || seconds / 3600 != drawnSecond_ / 3600) {
    sdd::drawClockDigit(tft_, 20, 82, seconds / 3600 / 10, 3, SD_FONT_WHITE);
    sdd::drawClockDigit(tft_, 60, 82, seconds / 3600 % 10, 3, SD_FONT_WHITE);
  }
  if (full || seconds / 60 != drawnSecond_ / 60) {
    sdd::drawClockDigit(tft_, 101, 82, seconds / 60 % 60 / 10, 3, SD_FONT_YELLOW);
    sdd::drawClockDigit(tft_, 141, 82, seconds / 60 % 10, 3, SD_FONT_YELLOW);
  }
  sdd::drawClockDigit(tft_, 182, 112, seconds % 60 / 10, 2, SD_FONT_WHITE);
  sdd::drawClockDigit(tft_, 202, 112, seconds % 10, 2, SD_FONT_WHITE);
}

void OfflineScreen::draw(const macstatus::OfflineClock &clock) {
  const bool repaint = !drawn_ || drawnValid_ != clock.valid();
  if (repaint) {
    tft_.fillScreen(theme::kBackground);
#ifndef ARDUINO
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextSize(1);
    tft_.setTextFont(theme::kFontLabel);
    tft_.setTextColor(theme::kMuted, theme::kBackground);
    tft_.drawString("LOCAL CLOCK", 120, 35);
    tft_.setTextFont(theme::kFontSmall);
    tft_.drawString("USB OFFLINE", 120, 201);
    tft_.drawString(clock.valid() ? "Reconnect to sync" : "Connect USB to set time", 120, 218);
#endif
    drawnSecond_ = UINT32_MAX;
  }
  const uint32_t seconds = clock.seconds();
  // The 90 px digit cells overlap the calendar band, so a minute change also
  // repaints the weather page on top of them.
  const bool pageRefresh = repaint || seconds / 60 != drawnSecond_ / 60;
  if (!repaint && drawnSecond_ == seconds) {
#ifdef ARDUINO
    weather_->draw(clock, false);
#endif
    return;
  }
  if (clock.valid()) {
    drawDigits(seconds, drawnSecond_ == UINT32_MAX);
  } else {
    tft_.fillRect(0, 76, 240, 72, theme::kBackground);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextFont(theme::kFontLabel);
    tft_.setTextSize(3);
    tft_.setTextColor(theme::kText, theme::kBackground);
    tft_.drawString("--:--", 120, 110);
    tft_.setTextSize(1);
  }
#ifdef ARDUINO
  weather_->draw(clock, pageRefresh);
#else
  (void)pageRefresh;
#endif
  drawnSecond_ = seconds;
  drawnValid_ = clock.valid();
  drawn_ = true;
}

}  // namespace minidisplay
