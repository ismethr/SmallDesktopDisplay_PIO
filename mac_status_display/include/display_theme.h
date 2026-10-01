#ifndef MINIDISPLAY_DISPLAY_THEME_H
#define MINIDISPLAY_DISPLAY_THEME_H

#include <stdint.h>

namespace minidisplay {
namespace theme {

// RGB565 palette shared by every status-screen view.
constexpr uint16_t kBackground = 0x0000;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kPanelBorder = 0x5ACB;  // Card outlines and progress-bar tracks.
constexpr uint16_t kMuted = 0xAD55;
constexpr uint16_t kUnknownFlag = 0x2104;
constexpr uint16_t kGreen = 0x4E69;
constexpr uint16_t kYellow = 0xF5C0;
constexpr uint16_t kRed = 0xF9E7;
constexpr uint16_t kBlue = 0x45BF;
constexpr uint16_t kPurple = 0xA35F;
constexpr uint16_t kCodex = kGreen;
constexpr uint16_t kClaude = 0xDBAA;

// The flag area is calibrated for this panel's observed red/blue ordering.
// Keep the rest of the established theme colors unchanged.
constexpr uint16_t flagPanelColor(uint16_t rgb565) {
  return static_cast<uint16_t>(((rgb565 & 0xF800U) >> 11) | (rgb565 & 0x07E0U) |
                               ((rgb565 & 0x001FU) << 11));
}

// Thresholds are in tenths of a percent (or of a degree Celsius).
constexpr uint16_t loadColor(int16_t tenths) {
  return tenths >= 850 ? kRed : tenths >= 650 ? kYellow : kGreen;
}

constexpr uint16_t temperatureColor(int16_t tenths) {
  return tenths >= 800 ? kRed : tenths >= 600 ? kYellow : kGreen;
}

// Remaining quota keeps its brand accent until it is running low.
constexpr uint16_t quotaColor(int16_t remainingTenths, uint16_t accent) {
  return remainingTenths < 100 ? kRed : remainingTenths < 300 ? kYellow : accent;
}

// TFT_eSPI built-in fonts. kFontValue must be enabled with LOAD_FONT4.
constexpr uint8_t kFontSmall = 1;   // 6 x 8 GLCD
constexpr uint8_t kFontLabel = 2;   // 16 px
constexpr uint8_t kFontValue = 4;   // 26 px

}  // namespace theme

struct Rect {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
};

}  // namespace minidisplay

#endif
