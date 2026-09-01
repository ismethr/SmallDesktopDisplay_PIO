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

  display.setTextDatum(TL_DATUM);
  display.setTextFont(1);
  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.drawString(loadLabel, 18, top + 1);
  display.drawString(temperatureLabel, 151, top + 1);

  char loadValue[8];
  snprintf(loadValue, sizeof(loadValue), "%u%%",
           static_cast<unsigned>((loadTenths + 5U) / 10U));
  display.setTextDatum(MC_DATUM);
  display.setTextFont(2);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, kBackground);
  display.drawString(loadValue, 73, top + 18);
  display.setTextSize(1);
  drawProgressBar(18, top + 30, 112, loadTenths, loadColor(loadTenths));

  const bool validTemperature = temperatureTenths != macstatus::kMissingTemperature;
  const uint16_t color = validTemperature ? temperatureColor(temperatureTenths) : kMuted;
  char temperatureValue[8];
  if (validTemperature) {
    snprintf(temperatureValue, sizeof(temperatureValue), "%d",
             static_cast<int>((temperatureTenths + 5) / 10));
  } else {
    snprintf(temperatureValue, sizeof(temperatureValue), "--");
  }
  display.setTextDatum(MR_DATUM);
  display.setTextFont(2);
  display.setTextSize(2);
  display.setTextColor(color, kBackground);
  display.drawString(temperatureValue, 204, top + 18);
  display.setTextSize(1);
  display.drawCircle(209, top + 12, 2, color);
  display.setTextDatum(ML_DATUM);
  display.setTextFont(1);
  display.drawString("C", 214, top + 18);
}

void formatRate(uint32_t bytesPerSecond, char *output, size_t outputSize) {
  if (bytesPerSecond < 1024U) {
    snprintf(output, outputSize, "%luB/s", static_cast<unsigned long>(bytesPerSecond));
    return;
  }
  const uint64_t divisor = bytesPerSecond < 1024UL * 1024UL ? 1024ULL : 1024ULL * 1024ULL;
  const char unit = bytesPerSecond < 1024UL * 1024UL ? 'K' : 'M';
  const uint64_t tenths = (static_cast<uint64_t>(bytesPerSecond) * 10ULL + divisor / 2ULL) / divisor;
  snprintf(output, outputSize, "%lu.%lu%c/s",
           static_cast<unsigned long>(tenths / 10ULL),
           static_cast<unsigned long>(tenths % 10ULL), unit);
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
  formatRate(download, downText, sizeof(downText));
  formatRate(upload, upText, sizeof(upText));
  display.fillRect(14, 205, 64, 21, kBackground);
  display.fillRect(86, 205, 67, 21, kBackground);
  display.fillRect(161, 205, 65, 21, kBackground);
  display.setTextDatum(MC_DATUM);
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(locationStale ? kMuted : TFT_WHITE, kBackground);
  display.drawString(location, 46, 217);
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
