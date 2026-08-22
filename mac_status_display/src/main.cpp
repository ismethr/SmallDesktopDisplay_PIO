#include <Arduino.h>
#include <TFT_eSPI.h>

#include "status_protocol.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kOfflineAfterMs = 4000;
constexpr uint16_t kBackground = 0x0861;
constexpr uint16_t kPanel = 0x10C3;
constexpr uint16_t kPanelBorder = 0x2965;
constexpr uint16_t kMuted = 0x9CD3;
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
  display.drawString("MAC STATUS", 10, 18);
  display.drawFastHLine(8, 35, 224, kPanelBorder);

  display.fillRoundRect(8, 44, 108, 82, 8, kPanel);
  display.drawRoundRect(8, 44, 108, 82, 8, kPanelBorder);
  display.fillRoundRect(124, 44, 108, 82, 8, kPanel);
  display.drawRoundRect(124, 44, 108, 82, 8, kPanelBorder);

  display.setTextDatum(TL_DATUM);
  display.setTextFont(2);
  display.setTextColor(kMuted, kPanel);
  display.drawString("CPU", 18, 51);
  display.drawString("MEMORY", 134, 51);

  display.fillRoundRect(8, 134, 224, 43, 8, kPanel);
  display.drawRoundRect(8, 134, 224, 43, 8, kPanelBorder);
  display.fillCircle(25, 156, 7, kRed);
  display.fillRoundRect(22, 141, 6, 20, 3, kRed);
  display.setTextDatum(ML_DATUM);
  display.setTextColor(kMuted, kPanel);
  display.drawString("CPU TEMP", 40, 156);

  display.fillRoundRect(8, 185, 224, 47, 8, kPanel);
  display.drawRoundRect(8, 185, 224, 47, 8, kPanelBorder);
  display.setTextFont(1);
  display.setTextColor(kBlue, kPanel);
  display.drawString("DOWN", 20, 195);
  display.setTextColor(kPurple, kPanel);
  display.drawString("UP", 137, 195);
  display.drawFastVLine(120, 191, 34, kPanelBorder);
  drawConnectionStatus("WAITING", kYellow);
}

void drawProgressBar(int16_t x, int16_t y, int16_t width, uint16_t tenths, uint16_t color) {
  display.fillRoundRect(x, y, width, 7, 3, kPanelBorder);
  const int16_t filled = static_cast<int16_t>((static_cast<uint32_t>(width) * tenths) / 1000U);
  if (filled > 0) display.fillRoundRect(x, y, filled, 7, 3, color);
}

void drawPercent(int16_t x, uint16_t tenths) {
  char value[8];
  snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>((tenths + 5U) / 10U));
  display.fillRect(x + 4, 69, 100, 37, kPanel);
  display.setTextDatum(MC_DATUM);
  display.setTextFont(2);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, kPanel);
  display.drawString(value, x + 54, 87);
  display.setTextSize(1);
  drawProgressBar(x + 11, 112, 86, tenths, loadColor(tenths));
}

void formatRate(uint32_t bytesPerSecond, char *output, size_t outputSize) {
  if (bytesPerSecond < 1024U) {
    snprintf(output, outputSize, "%lu B/s", static_cast<unsigned long>(bytesPerSecond));
    return;
  }
  const uint64_t divisor = bytesPerSecond < 1024UL * 1024UL ? 1024ULL : 1024ULL * 1024ULL;
  const char unit = bytesPerSecond < 1024UL * 1024UL ? 'K' : 'M';
  const uint64_t tenths = (static_cast<uint64_t>(bytesPerSecond) * 10ULL + divisor / 2ULL) / divisor;
  snprintf(output, outputSize, "%lu.%lu %c/s",
           static_cast<unsigned long>(tenths / 10ULL),
           static_cast<unsigned long>(tenths % 10ULL), unit);
}

void drawTemperature(int16_t tenths) {
  char value[12];
  uint16_t color = TFT_WHITE;
  if (tenths == macstatus::kMissingTemperature) {
    snprintf(value, sizeof(value), "--.- C");
    color = kYellow;
  } else {
    const bool negative = tenths < 0;
    const uint16_t magnitude = static_cast<uint16_t>(negative ? -tenths : tenths);
    snprintf(value, sizeof(value), "%s%u.%u C", negative ? "-" : "",
             static_cast<unsigned>(magnitude / 10U),
             static_cast<unsigned>(magnitude % 10U));
  }
  display.fillRect(132, 141, 92, 29, kPanel);
  display.setTextDatum(MR_DATUM);
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(color, kPanel);
  display.drawString(value, 218, 156);
}

void drawNetwork(uint32_t download, uint32_t upload) {
  char downText[16];
  char upText[16];
  formatRate(download, downText, sizeof(downText));
  formatRate(upload, upText, sizeof(upText));
  display.fillRect(14, 204, 99, 22, kPanel);
  display.fillRect(127, 204, 99, 22, kPanel);
  display.setTextDatum(MC_DATUM);
  display.setTextFont(2);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, kPanel);
  display.drawString(downText, 64, 215);
  display.drawString(upText, 177, 215);
}

void drawFrame(const macstatus::StatusFrame &frame) {
  drawPercent(8, frame.cpuTenths);
  drawPercent(124, frame.memoryTenths);
  drawTemperature(frame.temperatureTenths);
  drawNetwork(frame.downloadBytesPerSecond, frame.uploadBytesPerSecond);
  drawConnectionStatus("USB LIVE", kGreen);
  offlineDrawn = false;
}

void processLine() {
  if (serialOverflow || serialLength == 0) return;
  serialBuffer[serialLength] = '\0';
  macstatus::StatusFrame frame = {0, 0, 0, macstatus::kMissingTemperature, 0, 0};
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
  analogWrite(TFT_BL, 512);

  display.begin();
  display.invertDisplay(1);
  display.setRotation(0);
  drawStaticInterface();
  lastValidFrameAt = millis();
  Serial.println("MSD1 READY");
}

void loop() {
  readSerialFrames();
  if (!offlineDrawn && millis() - lastValidFrameAt > kOfflineAfterMs) {
    drawConnectionStatus("USB LOST", kRed);
    offlineDrawn = true;
  }
  delay(2);
}
