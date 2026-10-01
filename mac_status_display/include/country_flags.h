#ifndef MINIDISPLAY_COUNTRY_FLAGS_H
#define MINIDISPLAY_COUNTRY_FLAGS_H

#include <TFT_eSPI.h>

namespace minidisplay {

constexpr int16_t kFlagWidth = 24;
constexpr int16_t kFlagHeight = 14;

// Draws a 24 x 14 framed flag for an ISO 3166-1 alpha-2 code. Unknown codes
// fall back to the code itself on a neutral tile; stale data dims the frame.
void drawCountryFlag(TFT_eSPI &tft, int16_t x, int16_t y, const char country[3], bool stale);

}  // namespace minidisplay

#endif
