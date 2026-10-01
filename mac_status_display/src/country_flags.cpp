#include "country_flags.h"

#include <string.h>

#include "display_theme.h"

namespace minidisplay {
namespace {

using theme::flagPanelColor;

constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kFlagBlue = flagPanelColor(0x001F);
constexpr uint16_t kFlagRed = flagPanelColor(0xF800);
constexpr uint16_t kFlagYellow = flagPanelColor(0xFFE0);
constexpr uint16_t kFlagGreen = flagPanelColor(0x07E0);
constexpr uint16_t kFlagOrange = flagPanelColor(0xFD20);
constexpr uint16_t kFlagDarkBlue = flagPanelColor(0x0011);
constexpr uint16_t kFlagLightBlue = flagPanelColor(0x867F);

// Inner artwork area inside the one-pixel frame.
constexpr int16_t kInnerWidth = kFlagWidth - 2;
constexpr int16_t kInnerHeight = kFlagHeight - 2;

// Two- and three-band flags, horizontal unless `vertical`.
struct BandFlag {
  char country[3];
  uint16_t first;
  uint16_t second;
  uint16_t third;
  bool vertical;
};

const BandFlag kBandFlags[] = {
    {{'D', 'E', '\0'}, kBlack, kFlagRed, kFlagYellow, false},
    {{'F', 'R', '\0'}, kFlagBlue, kWhite, kFlagRed, true},
    {{'I', 'T', '\0'}, kFlagGreen, kWhite, kFlagRed, true},
    {{'N', 'L', '\0'}, kFlagRed, kWhite, kFlagBlue, false},
    {{'R', 'U', '\0'}, kWhite, kFlagBlue, kFlagRed, false},
    {{'U', 'A', '\0'}, kFlagBlue, kFlagBlue, kFlagYellow, false},
    {{'I', 'E', '\0'}, kFlagGreen, kWhite, kFlagOrange, true},
    {{'B', 'E', '\0'}, kBlack, kFlagYellow, kFlagRed, true},
    {{'R', 'O', '\0'}, kFlagBlue, kFlagYellow, kFlagRed, true},
    {{'P', 'L', '\0'}, kWhite, kWhite, kFlagRed, false},
    {{'I', 'D', '\0'}, kFlagRed, kFlagRed, kWhite, false},
    {{'A', 'T', '\0'}, kFlagRed, kWhite, kFlagRed, false},
    {{'H', 'U', '\0'}, kFlagRed, kWhite, kFlagGreen, false},
    {{'E', 'E', '\0'}, kFlagBlue, kBlack, kWhite, false},
    {{'L', 'T', '\0'}, kFlagYellow, kFlagGreen, kFlagRed, false},
    {{'L', 'U', '\0'}, kFlagRed, kWhite, kFlagLightBlue, false},
    {{'C', 'O', '\0'}, kFlagYellow, kFlagBlue, kFlagRed, false},
    {{'B', 'G', '\0'}, kWhite, kFlagGreen, kFlagRed, false},
    {{'E', 'S', '\0'}, kFlagRed, kFlagYellow, kFlagRed, false},
    {{'P', 'T', '\0'}, kFlagGreen, kFlagGreen, kFlagRed, true},
};

void drawBandFlag(TFT_eSPI &tft, int16_t x, int16_t y, const BandFlag &flag) {
  if (flag.vertical && flag.first == flag.second) {
    tft.fillRect(x, y, 9, kInnerHeight, flag.first);
    tft.fillRect(x + 9, y, 13, kInnerHeight, flag.third);
  } else if (flag.vertical && flag.second == flag.third) {
    tft.fillRect(x, y, 13, kInnerHeight, flag.first);
    tft.fillRect(x + 13, y, 9, kInnerHeight, flag.second);
  } else if (flag.vertical) {
    tft.fillRect(x, y, 7, kInnerHeight, flag.first);
    tft.fillRect(x + 7, y, 8, kInnerHeight, flag.second);
    tft.fillRect(x + 15, y, 7, kInnerHeight, flag.third);
  } else if (flag.first == flag.second) {
    tft.fillRect(x, y, kInnerWidth, 6, flag.first);
    tft.fillRect(x, y + 6, kInnerWidth, 6, flag.third);
  } else if (flag.second == flag.third) {
    tft.fillRect(x, y, kInnerWidth, 6, flag.first);
    tft.fillRect(x, y + 6, kInnerWidth, 6, flag.second);
  } else {
    tft.fillRect(x, y, kInnerWidth, 4, flag.first);
    tft.fillRect(x, y + 4, kInnerWidth, 4, flag.second);
    tft.fillRect(x, y + 8, kInnerWidth, 4, flag.third);
  }
}

void drawUnitedStates(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kWhite);
  for (int16_t stripe = 0; stripe < 6; stripe += 2) {
    tft.fillRect(x, y + stripe * 2, kInnerWidth, 2, kFlagRed);
  }
  tft.fillRect(x, y, 10, 7, kFlagDarkBlue);
  tft.drawPixel(x + 2, y + 2, kWhite);
  tft.drawPixel(x + 6, y + 2, kWhite);
  tft.drawPixel(x + 4, y + 5, kWhite);
}

void drawChina(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kFlagRed);
  tft.fillCircle(x + 5, y + 4, 2, kFlagYellow);
  tft.drawPixel(x + 10, y + 2, kFlagYellow);
  tft.drawPixel(x + 11, y + 5, kFlagYellow);
  tft.drawPixel(x + 9, y + 8, kFlagYellow);
}

void drawHongKong(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kFlagRed);
  tft.fillCircle(x + 11, y + 3, 1, kWhite);
  tft.fillCircle(x + 14, y + 5, 1, kWhite);
  tft.fillCircle(x + 13, y + 8, 1, kWhite);
  tft.fillCircle(x + 9, y + 8, 1, kWhite);
  tft.fillCircle(x + 8, y + 5, 1, kWhite);
}

void drawTaiwan(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kFlagRed);
  tft.fillRect(x, y, 10, 7, kFlagDarkBlue);
  tft.fillCircle(x + 5, y + 3, 2, kWhite);
}

void drawSingapore(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, 6, kFlagRed);
  tft.fillRect(x, y + 6, kInnerWidth, 6, kWhite);
  tft.fillCircle(x + 5, y + 3, 3, kWhite);
  tft.fillCircle(x + 6, y + 2, 2, kFlagRed);
  tft.drawPixel(x + 10, y + 2, kWhite);
  tft.drawPixel(x + 12, y + 4, kWhite);
}

void drawJapan(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kWhite);
  tft.fillCircle(x + 11, y + 6, 4, kFlagRed);
}

void drawKorea(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kWhite);
  tft.fillCircle(x + 11, y + 6, 4, kFlagRed);
  tft.fillRect(x + 7, y + 6, 8, 4, kFlagBlue);
  tft.drawFastHLine(x + 2, y + 3, 4, kBlack);
  tft.drawFastHLine(x + 16, y + 8, 4, kBlack);
}

void drawUnitedKingdom(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kFlagDarkBlue);
  tft.drawLine(x, y, x + 21, y + 11, kWhite);
  tft.drawLine(x + 21, y, x, y + 11, kWhite);
  tft.fillRect(x + 9, y, 4, kInnerHeight, kWhite);
  tft.fillRect(x, y + 4, kInnerWidth, 4, kWhite);
  tft.fillRect(x + 10, y, 2, kInnerHeight, kFlagRed);
  tft.fillRect(x, y + 5, kInnerWidth, 2, kFlagRed);
}

void drawCanada(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kWhite);
  tft.fillRect(x, y, 5, kInnerHeight, kFlagRed);
  tft.fillRect(x + 17, y, 5, kInnerHeight, kFlagRed);
  tft.fillTriangle(x + 11, y + 2, x + 8, y + 8, x + 14, y + 8, kFlagRed);
}

void drawBrazil(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kFlagGreen);
  tft.fillTriangle(x + 11, y + 1, x + 3, y + 6, x + 11, y + 11, kFlagYellow);
  tft.fillTriangle(x + 11, y + 1, x + 19, y + 6, x + 11, y + 11, kFlagYellow);
  tft.fillCircle(x + 11, y + 6, 3, kFlagBlue);
}

void drawSwitzerland(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kFlagRed);
  tft.fillRect(x + 9, y + 2, 4, 8, kWhite);
  tft.fillRect(x + 7, y + 4, 8, 4, kWhite);
}

void drawIndia(TFT_eSPI &tft, int16_t x, int16_t y) {
  tft.fillRect(x, y, kInnerWidth, 4, kFlagOrange);
  tft.fillRect(x, y + 4, kInnerWidth, 4, kWhite);
  tft.fillRect(x, y + 8, kInnerWidth, 4, kFlagGreen);
  tft.drawCircle(x + 11, y + 6, 2, kFlagBlue);
}

void drawSouthernCross(TFT_eSPI &tft, int16_t x, int16_t y, uint16_t starColor) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, kFlagDarkBlue);
  tft.fillRect(x + 4, y, 2, 7, kWhite);
  tft.fillRect(x, y + 2, 10, 2, kWhite);
  tft.fillRect(x + 4, y, 1, 7, kFlagRed);
  tft.fillRect(x, y + 3, 10, 1, kFlagRed);
  tft.fillCircle(x + 16, y + 3, 1, starColor);
  tft.fillCircle(x + 19, y + 7, 1, starColor);
  tft.fillCircle(x + 14, y + 9, 1, starColor);
}

void drawAustralia(TFT_eSPI &tft, int16_t x, int16_t y) { drawSouthernCross(tft, x, y, kWhite); }
void drawNewZealand(TFT_eSPI &tft, int16_t x, int16_t y) { drawSouthernCross(tft, x, y, kFlagRed); }

struct DetailedFlag {
  char country[3];
  void (*draw)(TFT_eSPI &, int16_t, int16_t);
};

const DetailedFlag kDetailedFlags[] = {
    {{'U', 'S', '\0'}, drawUnitedStates}, {{'C', 'N', '\0'}, drawChina},
    {{'H', 'K', '\0'}, drawHongKong},     {{'T', 'W', '\0'}, drawTaiwan},
    {{'S', 'G', '\0'}, drawSingapore},    {{'J', 'P', '\0'}, drawJapan},
    {{'K', 'R', '\0'}, drawKorea},        {{'G', 'B', '\0'}, drawUnitedKingdom},
    {{'C', 'A', '\0'}, drawCanada},       {{'B', 'R', '\0'}, drawBrazil},
    {{'C', 'H', '\0'}, drawSwitzerland},  {{'I', 'N', '\0'}, drawIndia},
    {{'A', 'U', '\0'}, drawAustralia},    {{'N', 'Z', '\0'}, drawNewZealand},
};

void drawUnknown(TFT_eSPI &tft, int16_t x, int16_t y, const char country[3]) {
  tft.fillRect(x, y, kInnerWidth, kInnerHeight, theme::kUnknownFlag);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(theme::kFontSmall);
  tft.setTextSize(1);
  tft.setTextColor(theme::kMuted, theme::kUnknownFlag);
  tft.drawString(country, x + kInnerWidth / 2, y + kInnerHeight / 2);
}

}  // namespace

void drawCountryFlag(TFT_eSPI &tft, int16_t x, int16_t y, const char country[3], bool stale) {
  const int16_t innerX = x + 1;
  const int16_t innerY = y + 1;
  tft.fillRect(innerX, innerY, kInnerWidth, kInnerHeight, theme::kBackground);

  bool drawn = false;
  for (const DetailedFlag &flag : kDetailedFlags) {
    if (strcmp(country, flag.country) == 0) {
      flag.draw(tft, innerX, innerY);
      drawn = true;
      break;
    }
  }
  for (size_t index = 0; !drawn && index < sizeof(kBandFlags) / sizeof(kBandFlags[0]); ++index) {
    if (strcmp(country, kBandFlags[index].country) == 0) {
      drawBandFlag(tft, innerX, innerY, kBandFlags[index]);
      drawn = true;
    }
  }
  if (!drawn) drawUnknown(tft, innerX, innerY, country);
  tft.drawRect(x, y, kFlagWidth, kFlagHeight, stale ? theme::kMuted : theme::kPanelBorder);
}

}  // namespace minidisplay
