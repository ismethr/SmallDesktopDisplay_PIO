#include <Arduino.h>
#include <TFT_eSPI.h>

#include "status_protocol.h"
#include "../../src/img/chatgpt_24.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kOfflineAfterMs = 4000;
constexpr uint8_t kDefaultDayBrightness = 50;
constexpr uint8_t kDefaultOfflineBrightness = 5;
constexpr uint16_t kBackground = TFT_BLACK;
constexpr uint16_t kPanelBorder = 0x5ACB;
constexpr uint16_t kMuted = 0xAD55;
constexpr uint16_t kGreen = 0x4E69;
constexpr uint16_t kYellow = 0xF5C0;
constexpr uint16_t kRed = 0xF9E7;
constexpr uint16_t kBlue = 0x45BF;
constexpr uint16_t kPurple = 0xA35F;
constexpr uint16_t flagPanelColor(uint16_t rgb565) {
  return static_cast<uint16_t>(((rgb565 & 0xF800U) >> 11) |
                               (rgb565 & 0x07E0U) |
                               ((rgb565 & 0x001FU) << 11));
}

// The flag area is calibrated for this panel's observed red/blue ordering.
// Keep the rest of the established theme colors unchanged.
constexpr uint16_t kFlagBlue = flagPanelColor(0x001F);
constexpr uint16_t kFlagRed = flagPanelColor(0xF800);
constexpr uint16_t kFlagYellow = flagPanelColor(0xFFE0);
constexpr uint16_t kFlagGreen = flagPanelColor(0x07E0);
constexpr uint16_t kFlagOrange = flagPanelColor(0xFD20);
constexpr uint16_t kFlagDarkBlue = flagPanelColor(0x0011);

TFT_eSPI display;
char serialBuffer[macstatus::kMaximumFrameLength + 1] = {};
size_t serialLength = 0;
bool serialOverflow = false;
bool offlineDrawn = false;
uint32_t lastValidFrameAt = 0;
uint8_t currentBrightness = 255;
uint8_t offlineBrightness = kDefaultOfflineBrightness;

void drawCodexUsage(int16_t remainingTenths, bool stale);
void drawMetricRow(int16_t top, const char *loadLabel, uint16_t loadTenths,
                   const char *temperatureLabel, int16_t temperatureTenths);

void drawDashedHorizontalLine(int16_t x, int16_t y, int16_t length,
                              uint16_t color) {
  constexpr int16_t kDashLength = 5;
  constexpr int16_t kDashGap = 4;
  for (int16_t offset = 0; offset < length; offset += kDashLength + kDashGap) {
    const int16_t remaining = length - offset;
    const int16_t segment = remaining < kDashLength ? remaining : kDashLength;
    display.drawFastHLine(x + offset, y, segment, color);
  }
}

void drawDashedVerticalLine(int16_t x, int16_t y, int16_t length,
                            uint16_t color) {
  constexpr int16_t kDashLength = 5;
  constexpr int16_t kDashGap = 4;
  for (int16_t offset = 0; offset < length; offset += kDashLength + kDashGap) {
    const int16_t remaining = length - offset;
    const int16_t segment = remaining < kDashLength ? remaining : kDashLength;
    display.drawFastVLine(x, y + offset, segment, color);
  }
}

void drawDashedBorder(int16_t x, int16_t y, int16_t width, int16_t height,
                      uint16_t color) {
  drawDashedHorizontalLine(x, y, width, color);
  drawDashedHorizontalLine(x, y + height - 1, width, color);
  drawDashedVerticalLine(x, y, height, color);
  drawDashedVerticalLine(x + width - 1, y, height, color);
}

void applyBrightness(uint8_t percent) {
  if (percent == currentBrightness) return;
  const uint16_t pwm = static_cast<uint16_t>(
      1023U - (static_cast<uint32_t>(percent) * 1023U) / 100U);
  analogWrite(TFT_BL, pwm);
  currentBrightness = percent;
}

uint16_t loadColor(uint16_t tenths) {
  if (tenths >= 850) return kRed;
  if (tenths >= 650) return kYellow;
  return kGreen;
}

void drawConnectionStatus(const char *label, uint16_t color) {
  display.fillRect(148, 7, 84, 22, kBackground);
  display.fillCircle(156, 17, 4, color);
  display.setTextDatum(ML_DATUM);
  display.setTextFont(1);
  display.setTextSize(1);
  display.setTextColor(color, kBackground);
  display.drawString(label, 166, 17);
}

void drawStaticInterface() {
  display.fillScreen(kBackground);
  display.setTextDatum(ML_DATUM);
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, kBackground);
  display.drawString("SYSTEM STATUS", 10, 18);
  display.drawFastHLine(8, 35, 224, kPanelBorder);

  drawDashedBorder(8, 44, 224, 87, kPanelBorder);
  drawDashedHorizontalLine(14, 87, 212, kPanelBorder);
  drawMetricRow(48, "CPU LOAD", 0, "CPU TEMP", macstatus::kMissingTemperature);
  drawMetricRow(91, "MEMORY", 0, "GPU TEMP", macstatus::kMissingTemperature);

  drawDashedBorder(8, 138, 224, 42, kPanelBorder);
  drawCodexUsage(macstatus::kMissingCodexUsage, false);

  drawDashedBorder(8, 188, 224, 44, kPanelBorder);
  display.setTextFont(1);
  display.setTextColor(kGreen, kBackground);
  display.drawString("LOCATION", 16, 196);
  display.setTextColor(kBlue, kBackground);
  display.drawString("DOWN", 94, 196);
  display.setTextColor(kPurple, kBackground);
  display.drawString("UP", 170, 196);
  drawDashedVerticalLine(82, 194, 31, kPanelBorder);
  drawDashedVerticalLine(157, 194, 31, kPanelBorder);
  drawConnectionStatus("WAITING", kYellow);
}

void drawProgressBar(int16_t x, int16_t y, int16_t width, uint16_t tenths, uint16_t color) {
  display.fillRoundRect(x, y, width, 7, 3, kPanelBorder);
  const int16_t filled = static_cast<int16_t>((static_cast<uint32_t>(width) * tenths) / 1000U);
  if (filled > 0) display.fillRoundRect(x, y, filled, 7, 3, color);
}

uint16_t temperatureColor(int16_t tenths) {
  if (tenths >= 800) return kRed;
  if (tenths >= 600) return kYellow;
  return kGreen;
}

void drawMetricRow(int16_t top, const char *loadLabel, uint16_t loadTenths,
                   const char *temperatureLabel, int16_t temperatureTenths) {
  display.fillRect(14, top, 212, 36, kBackground);
  drawDashedVerticalLine(140, top + 5, 27, kPanelBorder);

  display.setTextDatum(TL_DATUM);
  display.setTextFont(1);
  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.drawString(loadLabel, 18, top + 1);
  display.setTextDatum(TR_DATUM);
  display.drawString(temperatureLabel, 219, top + 1);

  char loadValue[8];
  snprintf(loadValue, sizeof(loadValue), "%u%%",
           static_cast<unsigned>((loadTenths + 5U) / 10U));
  display.setTextDatum(TL_DATUM);
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, kBackground);
  display.drawString(loadValue, 18, top + 12);
  drawProgressBar(18, top + 30, 112, loadTenths, loadColor(loadTenths));

  const bool validTemperature = temperatureTenths != macstatus::kMissingTemperature;
  const uint16_t color = validTemperature ? temperatureColor(temperatureTenths) : kMuted;
  char temperatureValue[10];
  if (validTemperature) {
    snprintf(temperatureValue, sizeof(temperatureValue), "%d`C",
             static_cast<int>((temperatureTenths + 5) / 10));
  } else {
    snprintf(temperatureValue, sizeof(temperatureValue), "--`C");
  }
  display.setTextDatum(TR_DATUM);
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(color, kBackground);
  display.drawString(temperatureValue, 219, top + 12);
}

void formatRate(uint32_t bytesPerSecond, char *output, size_t outputSize) {
  if (bytesPerSecond < 1024U) {
    snprintf(output, outputSize, "%luB/s", static_cast<unsigned long>(bytesPerSecond));
    return;
  }
  uint64_t divisor = 1024ULL;
  char unit = 'K';
  if (bytesPerSecond >= 1024UL * 1024UL * 1024UL) {
    divisor = 1024ULL * 1024ULL * 1024ULL;
    unit = 'G';
  } else if (bytesPerSecond >= 1024UL * 1024UL) {
    divisor = 1024ULL * 1024ULL;
    unit = 'M';
  }
  const uint64_t tenths = (static_cast<uint64_t>(bytesPerSecond) * 10ULL + divisor / 2ULL) / divisor;
  snprintf(output, outputSize, "%lu.%lu%c/s",
           static_cast<unsigned long>(tenths / 10ULL),
           static_cast<unsigned long>(tenths % 10ULL), unit);
}

struct SimpleFlag {
  char country[3];
  uint16_t first;
  uint16_t second;
  uint16_t third;
  bool vertical;
};

const SimpleFlag kSimpleFlags[] = {
    {{'D', 'E', '\0'}, TFT_BLACK, kFlagRed, kFlagYellow, false},
    {{'F', 'R', '\0'}, kFlagBlue, TFT_WHITE, kFlagRed, true},
    {{'I', 'T', '\0'}, kFlagGreen, TFT_WHITE, kFlagRed, true},
    {{'N', 'L', '\0'}, kFlagRed, TFT_WHITE, kFlagBlue, false},
    {{'R', 'U', '\0'}, TFT_WHITE, kFlagBlue, kFlagRed, false},
    {{'U', 'A', '\0'}, kFlagBlue, kFlagBlue, kFlagYellow, false},
    {{'I', 'E', '\0'}, kFlagGreen, TFT_WHITE, kFlagOrange, true},
    {{'B', 'E', '\0'}, TFT_BLACK, kFlagYellow, kFlagRed, true},
    {{'R', 'O', '\0'}, kFlagBlue, kFlagYellow, kFlagRed, true},
    {{'P', 'L', '\0'}, TFT_WHITE, TFT_WHITE, kFlagRed, false},
    {{'I', 'D', '\0'}, kFlagRed, kFlagRed, TFT_WHITE, false},
    {{'A', 'T', '\0'}, kFlagRed, TFT_WHITE, kFlagRed, false},
    {{'H', 'U', '\0'}, kFlagRed, TFT_WHITE, kFlagGreen, false},
    {{'E', 'E', '\0'}, kFlagBlue, TFT_BLACK, TFT_WHITE, false},
    {{'L', 'T', '\0'}, kFlagYellow, kFlagGreen, kFlagRed, false},
    {{'L', 'U', '\0'}, kFlagRed, TFT_WHITE, flagPanelColor(0x867F), false},
    {{'C', 'O', '\0'}, kFlagYellow, kFlagBlue, kFlagRed, false},
    {{'B', 'G', '\0'}, TFT_WHITE, kFlagGreen, kFlagRed, false},
    {{'E', 'S', '\0'}, kFlagRed, kFlagYellow, kFlagRed, false},
    {{'P', 'T', '\0'}, kFlagGreen, kFlagGreen, kFlagRed, true},
};

void drawSimpleFlag(int16_t x, int16_t y, const SimpleFlag &flag) {
  constexpr int16_t kWidth = 22;
  constexpr int16_t kHeight = 12;
  if (flag.vertical && flag.first == flag.second) {
    display.fillRect(x, y, 9, kHeight, flag.first);
    display.fillRect(x + 9, y, 13, kHeight, flag.third);
  } else if (flag.vertical && flag.second == flag.third) {
    display.fillRect(x, y, 13, kHeight, flag.first);
    display.fillRect(x + 13, y, 9, kHeight, flag.second);
  } else if (flag.vertical) {
    display.fillRect(x, y, 7, kHeight, flag.first);
    display.fillRect(x + 7, y, 8, kHeight, flag.second);
    display.fillRect(x + 15, y, 7, kHeight, flag.third);
  } else if (flag.first == flag.second) {
    display.fillRect(x, y, kWidth, 6, flag.first);
    display.fillRect(x, y + 6, kWidth, 6, flag.third);
  } else if (flag.second == flag.third) {
    display.fillRect(x, y, kWidth, 6, flag.first);
    display.fillRect(x, y + 6, kWidth, 6, flag.second);
  } else {
    display.fillRect(x, y, kWidth, 4, flag.first);
    display.fillRect(x, y + 4, kWidth, 4, flag.second);
    display.fillRect(x, y + 8, kWidth, 4, flag.third);
  }
}

void drawCountryFlag(int16_t x, int16_t y, const char country[3], bool stale) {
  constexpr int16_t kWidth = 24;
  constexpr int16_t kHeight = 14;
  const int16_t innerX = x + 1;
  const int16_t innerY = y + 1;
  display.fillRect(innerX, innerY, kWidth - 2, kHeight - 2, kBackground);

  if (strcmp(country, "US") == 0) {
    display.fillRect(innerX, innerY, 22, 12, TFT_WHITE);
    for (int16_t stripe = 0; stripe < 6; stripe += 2) {
      display.fillRect(innerX, innerY + stripe * 2, 22, 2, kFlagRed);
    }
    display.fillRect(innerX, innerY, 10, 7, kFlagDarkBlue);
    display.drawPixel(innerX + 2, innerY + 2, TFT_WHITE);
    display.drawPixel(innerX + 6, innerY + 2, TFT_WHITE);
    display.drawPixel(innerX + 4, innerY + 5, TFT_WHITE);
  } else if (strcmp(country, "CN") == 0) {
    display.fillRect(innerX, innerY, 22, 12, kFlagRed);
    display.fillCircle(innerX + 5, innerY + 4, 2, kFlagYellow);
    display.drawPixel(innerX + 10, innerY + 2, kFlagYellow);
    display.drawPixel(innerX + 11, innerY + 5, kFlagYellow);
    display.drawPixel(innerX + 9, innerY + 8, kFlagYellow);
  } else if (strcmp(country, "HK") == 0) {
    display.fillRect(innerX, innerY, 22, 12, kFlagRed);
    display.fillCircle(innerX + 11, innerY + 3, 1, TFT_WHITE);
    display.fillCircle(innerX + 14, innerY + 5, 1, TFT_WHITE);
    display.fillCircle(innerX + 13, innerY + 8, 1, TFT_WHITE);
    display.fillCircle(innerX + 9, innerY + 8, 1, TFT_WHITE);
    display.fillCircle(innerX + 8, innerY + 5, 1, TFT_WHITE);
  } else if (strcmp(country, "TW") == 0) {
    display.fillRect(innerX, innerY, 22, 12, kFlagRed);
    display.fillRect(innerX, innerY, 10, 7, kFlagDarkBlue);
    display.fillCircle(innerX + 5, innerY + 3, 2, TFT_WHITE);
  } else if (strcmp(country, "SG") == 0) {
    display.fillRect(innerX, innerY, 22, 6, kFlagRed);
    display.fillRect(innerX, innerY + 6, 22, 6, TFT_WHITE);
    display.fillCircle(innerX + 5, innerY + 3, 3, TFT_WHITE);
    display.fillCircle(innerX + 6, innerY + 2, 2, kFlagRed);
    display.drawPixel(innerX + 10, innerY + 2, TFT_WHITE);
    display.drawPixel(innerX + 12, innerY + 4, TFT_WHITE);
  } else if (strcmp(country, "JP") == 0) {
    display.fillRect(innerX, innerY, 22, 12, TFT_WHITE);
    display.fillCircle(innerX + 11, innerY + 6, 4, kFlagRed);
  } else if (strcmp(country, "KR") == 0) {
    display.fillRect(innerX, innerY, 22, 12, TFT_WHITE);
    display.fillCircle(innerX + 11, innerY + 6, 4, kFlagRed);
    display.fillRect(innerX + 7, innerY + 6, 8, 4, kFlagBlue);
    display.drawFastHLine(innerX + 2, innerY + 3, 4, TFT_BLACK);
    display.drawFastHLine(innerX + 16, innerY + 8, 4, TFT_BLACK);
  } else if (strcmp(country, "GB") == 0) {
    display.fillRect(innerX, innerY, 22, 12, kFlagDarkBlue);
    display.drawLine(innerX, innerY, innerX + 21, innerY + 11, TFT_WHITE);
    display.drawLine(innerX + 21, innerY, innerX, innerY + 11, TFT_WHITE);
    display.fillRect(innerX + 9, innerY, 4, 12, TFT_WHITE);
    display.fillRect(innerX, innerY + 4, 22, 4, TFT_WHITE);
    display.fillRect(innerX + 10, innerY, 2, 12, kFlagRed);
    display.fillRect(innerX, innerY + 5, 22, 2, kFlagRed);
  } else if (strcmp(country, "CA") == 0) {
    display.fillRect(innerX, innerY, 22, 12, TFT_WHITE);
    display.fillRect(innerX, innerY, 5, 12, kFlagRed);
    display.fillRect(innerX + 17, innerY, 5, 12, kFlagRed);
    display.fillTriangle(innerX + 11, innerY + 2, innerX + 8, innerY + 8,
                         innerX + 14, innerY + 8, kFlagRed);
  } else if (strcmp(country, "BR") == 0) {
    display.fillRect(innerX, innerY, 22, 12, kFlagGreen);
    display.fillTriangle(innerX + 11, innerY + 1, innerX + 3, innerY + 6,
                         innerX + 11, innerY + 11, kFlagYellow);
    display.fillTriangle(innerX + 11, innerY + 1, innerX + 19, innerY + 6,
                         innerX + 11, innerY + 11, kFlagYellow);
    display.fillCircle(innerX + 11, innerY + 6, 3, kFlagBlue);
  } else if (strcmp(country, "CH") == 0) {
    display.fillRect(innerX, innerY, 22, 12, kFlagRed);
    display.fillRect(innerX + 9, innerY + 2, 4, 8, TFT_WHITE);
    display.fillRect(innerX + 7, innerY + 4, 8, 4, TFT_WHITE);
  } else if (strcmp(country, "IN") == 0) {
    display.fillRect(innerX, innerY, 22, 4, kFlagOrange);
    display.fillRect(innerX, innerY + 4, 22, 4, TFT_WHITE);
    display.fillRect(innerX, innerY + 8, 22, 4, kFlagGreen);
    display.drawCircle(innerX + 11, innerY + 6, 2, kFlagBlue);
  } else if (strcmp(country, "AU") == 0 || strcmp(country, "NZ") == 0) {
    display.fillRect(innerX, innerY, 22, 12, kFlagDarkBlue);
    display.fillRect(innerX + 4, innerY, 2, 7, TFT_WHITE);
    display.fillRect(innerX, innerY + 2, 10, 2, TFT_WHITE);
    display.fillRect(innerX + 4, innerY, 1, 7, kFlagRed);
    display.fillRect(innerX, innerY + 3, 10, 1, kFlagRed);
    const uint16_t starColor = strcmp(country, "NZ") == 0 ? kFlagRed : TFT_WHITE;
    display.fillCircle(innerX + 16, innerY + 3, 1, starColor);
    display.fillCircle(innerX + 19, innerY + 7, 1, starColor);
    display.fillCircle(innerX + 14, innerY + 9, 1, starColor);
  } else {
    bool matched = false;
    for (const SimpleFlag &flag : kSimpleFlags) {
      if (strcmp(country, flag.country) == 0) {
        drawSimpleFlag(innerX, innerY, flag);
        matched = true;
        break;
      }
    }
    if (!matched) {
      display.fillRect(innerX, innerY, 22, 12, 0x2104);
      display.setTextDatum(MC_DATUM);
      display.setTextFont(1);
      display.setTextSize(1);
      display.setTextColor(kMuted, 0x2104);
      display.drawString(country, innerX + 11, innerY + 6);
    }
  }
  display.drawRect(x, y, kWidth, kHeight, stale ? kMuted : kPanelBorder);
}

void splitNetworkLocation(const char *location, char country[3], char detail[4]) {
  strcpy(country, "--");
  strcpy(detail, "--");
  if (location == nullptr || strlen(location) < 2 || location[0] == '-') return;
  country[0] = location[0];
  country[1] = location[1];
  country[2] = '\0';
  const char *separator = strchr(location, '-');
  if (separator == nullptr || separator[1] == '\0') {
    strcpy(detail, country);
    return;
  }
  strncpy(detail, separator + 1, 3);
  detail[3] = '\0';
}

void drawChatGptIcon(int16_t x, int16_t y, uint16_t color) {
  for (uint8_t row = 0; row < 24; ++row) {
    for (uint8_t column = 0; column < 24; ++column) {
      const uint8_t bits = pgm_read_byte(chatgptIcon24 + row * 3 + column / 8);
      if ((bits & (0x80 >> (column % 8))) != 0) {
        display.drawPixel(x + column, y + row, color);
      }
    }
  }
}

void drawCodexUsage(int16_t remainingTenths, bool stale) {
  const bool valid = remainingTenths != macstatus::kMissingCodexUsage;
  uint16_t color = kMuted;
  if (valid && !stale) {
    color = remainingTenths >= 400 ? kGreen : (remainingTenths >= 150 ? kYellow : kRed);
  }

  display.fillRect(14, 144, 212, 30, kBackground);
  drawChatGptIcon(17, 147, valid && !stale ? TFT_WHITE : kMuted);

  display.setTextDatum(ML_DATUM);
  display.setTextFont(1);
  display.setTextSize(1);
  display.setTextColor(stale ? kMuted : TFT_WHITE, kBackground);
  display.drawString("CODEX LEFT", 49, 152);

  char value[8];
  if (valid) {
    snprintf(value, sizeof(value), "%u%%",
             static_cast<unsigned>((remainingTenths + 5) / 10));
  } else {
    snprintf(value, sizeof(value), "--");
  }
  display.setTextDatum(MR_DATUM);
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(valid && !stale ? TFT_WHITE : kMuted, kBackground);
  display.drawString(value, 218, 152);

  constexpr int16_t kBarX = 49;
  constexpr int16_t kBarY = 165;
  constexpr int16_t kBarWidth = 169;
  display.drawRoundRect(kBarX, kBarY, kBarWidth, 7, 3, kPanelBorder);
  if (valid && remainingTenths > 0) {
    const int16_t filled = static_cast<int16_t>(
        (static_cast<uint32_t>(kBarWidth - 2) * remainingTenths + 500U) / 1000U);
    if (filled >= 4) {
      display.fillRoundRect(kBarX + 1, kBarY + 1, filled, 5, 2, color);
    } else {
      display.fillRect(kBarX + 1, kBarY + 1, filled, 5, color);
    }
  }
}

void drawNetwork(uint32_t download, uint32_t upload, const char *location,
                 bool locationStale) {
  char downText[16];
  char upText[16];
  char country[3];
  char locationDetail[4];
  formatRate(download, downText, sizeof(downText));
  formatRate(upload, upText, sizeof(upText));
  splitNetworkLocation(location, country, locationDetail);
  display.fillRect(14, 205, 64, 21, kBackground);
  display.fillRect(86, 205, 67, 21, kBackground);
  display.fillRect(161, 205, 65, 21, kBackground);
  drawCountryFlag(15, 208, country, locationStale);
  display.setTextDatum(MC_DATUM);
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(locationStale ? kMuted : TFT_WHITE, kBackground);
  display.drawString(locationDetail, 61, 215);
  display.setTextColor(TFT_WHITE, kBackground);
  display.drawString(downText, 119, 217);
  display.drawString(upText, 194, 217);
}

void drawFrame(const macstatus::StatusFrame &frame) {
  offlineBrightness = frame.offlineBrightnessPercent;
  applyBrightness(frame.brightnessPercent);
  drawMetricRow(48, "CPU LOAD", frame.cpuTenths, "CPU TEMP",
                frame.cpuTemperatureTenths);
  drawMetricRow(91, "MEMORY", frame.memoryTenths, "GPU TEMP",
                frame.gpuTemperatureTenths);
  drawCodexUsage(frame.codexRemainingTenths, frame.codexUsageStale);
  drawNetwork(frame.downloadBytesPerSecond, frame.uploadBytesPerSecond,
              frame.networkLocation, frame.networkLocationStale);
  drawConnectionStatus("USB LIVE", kGreen);
  offlineDrawn = false;
}

void processLine() {
  if (serialOverflow || serialLength == 0) return;
  serialBuffer[serialLength] = '\0';
  macstatus::StatusFrame frame;
  memset(&frame, 0, sizeof(frame));
  frame.cpuTemperatureTenths = macstatus::kMissingTemperature;
  frame.gpuTemperatureTenths = macstatus::kMissingTemperature;
  frame.codexRemainingTenths = macstatus::kMissingCodexUsage;
  strncpy(frame.networkLocation, "--", sizeof(frame.networkLocation) - 1);
  frame.brightnessPercent = kDefaultDayBrightness;
  frame.offlineBrightnessPercent = kDefaultOfflineBrightness;
  if (!macstatus::parseStatusFrame(serialBuffer, frame)) return;
  lastValidFrameAt = millis();
  drawFrame(frame);
}

void readSerialFrames() {
  while (Serial.available() > 0) {
    const char next = static_cast<char>(Serial.read());
    if (next == '\n') {
      processLine();
      serialLength = 0;
      serialOverflow = false;
    } else if (next != '\r') {
      if (serialLength < macstatus::kMaximumFrameLength) {
        serialBuffer[serialLength++] = next;
      } else {
        serialOverflow = true;
      }
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  pinMode(TFT_BL, OUTPUT);
  analogWriteRange(1023);
  applyBrightness(kDefaultDayBrightness);

  display.begin();
  display.invertDisplay(1);
  display.setRotation(0);
  drawStaticInterface();
  lastValidFrameAt = millis();
  Serial.println("MSD4 READY");
}

void loop() {
  readSerialFrames();
  if (!offlineDrawn && millis() - lastValidFrameAt > kOfflineAfterMs) {
    drawConnectionStatus("USB LOST", kRed);
    applyBrightness(offlineBrightness);
    offlineDrawn = true;
  }
  delay(2);
}
