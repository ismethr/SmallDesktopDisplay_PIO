// Renders the real MiniDisplay status-screen code into 240 x 240 PPM files and
// checks layout, redraw and page-switching invariants without hardware.
// Every state is driven through CRC-framed USB lines, like the bridge sends.
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <TFT_eSPI.h>

#include "minidisplay_app.h"

namespace {

using macstatus::StatusFrame;
using minidisplay::MiniDisplayApp;

std::string directory;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void send(MiniDisplayApp &app, const std::string &payload, uint32_t now) {
  char checksum[8];
  snprintf(checksum, sizeof(checksum), "*%04X",
           macstatus::crc16Ccitt(reinterpret_cast<const uint8_t *>(payload.data()), payload.size()));
  std::string line = "$" + payload + checksum;
  std::vector<char> buffer(line.begin(), line.end());
  buffer.push_back('\0');
  app.handleLine(buffer.data(), now);
}

void sendStatus(MiniDisplayApp &app, const StatusFrame &frame, uint32_t now) {
  char payload[160];
  snprintf(payload, sizeof(payload), "MSD4,%u,%u,%u,%d,%d,%d,%u,%lu,%lu,%s,%u,%u,%u",
           frame.sequence, frame.cpuTenths, frame.memoryTenths, frame.cpuTemperatureTenths,
           frame.gpuTemperatureTenths, frame.codexRemainingTenths, frame.codexUsageStale ? 1U : 0U,
           static_cast<unsigned long>(frame.downloadBytesPerSecond),
           static_cast<unsigned long>(frame.uploadBytesPerSecond), frame.networkLocation,
           frame.networkLocationStale ? 1U : 0U, frame.brightnessPercent, frame.offlineBrightnessPercent);
  send(app, payload, now);
}

void sendQuota(MiniDisplayApp &app, const char *tag, int fiveHour, int week, bool stale, uint32_t now) {
  send(app, std::string(tag) + "," + std::to_string(fiveHour) + "," + std::to_string(week) + "," +
                (stale ? "1" : "0"), now);
}

void sendClaude(MiniDisplayApp &app, int fiveHour, int week, bool stale, uint32_t now) {
  sendQuota(app, "MSA1", fiveHour, week, stale, now);
}

void sendCodex(MiniDisplayApp &app, int fiveHour, int week, bool stale, uint32_t now) {
  sendQuota(app, "MSA2", fiveHour, week, stale, now);
}

void save(const TFT_eSPI &display, const char *name) { display.save(directory + "/" + name + ".ppm"); }

StatusFrame typicalFrame() {
  StatusFrame frame{};
  frame.cpuTenths = 347;
  frame.memoryTenths = 628;
  frame.cpuTemperatureTenths = 510;
  frame.gpuTemperatureTenths = 640;
  frame.codexRemainingTenths = 170;
  frame.downloadBytesPerSecond = 12500000;
  frame.uploadBytesPerSecond = 347000;
  strcpy(frame.networkLocation, "US-CA");
  frame.brightnessPercent = 50;
  frame.offlineBrightnessPercent = 5;
  return frame;
}

void checkFonts() {
  TFT_eSPI fontCheck;
  fontCheck.setTextFont(2);
  fontCheck.drawString("%", 0, 0);
  for (int row = 0; row < 16; ++row)
    for (int column = 0; column < 9; ++column) {
      const bool ink = column < 8 && (chr_f16_25[row] & (0x80 >> column));
      require(fontCheck.pixels[row * 240 + column] == (ink ? TFT_WHITE : TFT_BLACK),
              "font 2 raster differs from TFT_eSPI");
    }
  TFT_eSPI::verifyRleFont();
}

// Live states: every field, the header clock, quota colors and redraw economy.
void renderLiveStates() {
  TFT_eSPI display;
  MiniDisplayApp app(display);
  app.begin(0);
  save(display, "waiting");
  require(app.brightness() == MiniDisplayApp::kDefaultDayBrightness, "app.brightness() == MiniDisplayApp::kDefaultDayBrightness");

  StatusFrame frame = typicalFrame();
  send(app, "MSC2,1790800747", 1000);  // 2026-09-30 20:39:07 local
  send(app, "MSC1,74347", 1000);
  sendStatus(app, frame, 1000);
  app.tick(1000);
  // A bridge without MSA2 still fills the Codex weekly row from MSD4.
  save(display, "codex-weekly-only");
  require(app.codexUsage().fiveHourTenths == -1 && app.codexUsage().weekTenths == 170,
          "MSD4 supplies only the Codex weekly window");
  sendCodex(app, 580, 170, false, 1000);
  sendClaude(app, 730, 420, false, 1000);
  save(display, "live");
  require(!app.offline() && app.brightness() == 50, "!app.offline() && app.brightness() == 50");
  require(app.codexUsage().fiveHourTenths == 580, "MSA2 supplies the Codex 5-hour window");

  // Identical data and a clock inside the same minute must not touch the panel.
  size_t before = display.writes;
  sendStatus(app, frame, 2000);
  sendCodex(app, 580, 170, false, 2000);
  sendClaude(app, 730, 420, false, 2000);
  app.tick(2000);
  require(display.writes == before, "display.writes == before");
  // The header repaints when the minute changes.
  frame.sequence = 1;
  sendStatus(app, frame, 54500);
  app.tick(54500);
  require(display.writes > before, "display.writes > before");

  for (uint32_t rate : {0U, 1023U, 1024U, 1023488U, 1048575U, 1073741823U, UINT32_MAX}) {
    frame.downloadBytesPerSecond = frame.uploadBytesPerSecond = rate;
    sendStatus(app, frame, 55000);
  }
  frame.cpuTenths = frame.memoryTenths = 1000;
  frame.cpuTemperatureTenths = frame.gpuTemperatureTenths = 1500;
  frame.codexRemainingTenths = 1000;
  strcpy(frame.networkLocation, "US-WWW");
  sendStatus(app, frame, 55000);
  sendCodex(app, 1000, 1000, false, 55000);
  sendClaude(app, 1000, 1000, false, 55000);
  save(display, "maximum");

  frame = typicalFrame();
  frame.cpuTenths = 912;
  frame.memoryTenths = 704;
  frame.codexRemainingTenths = 84;
  strcpy(frame.networkLocation, "JP-13");
  sendStatus(app, frame, 56000);
  sendCodex(app, 40, 84, false, 56000);
  sendClaude(app, 255, 60, false, 56000);
  save(display, "low-quota");

  frame.codexUsageStale = true;
  frame.networkLocationStale = true;
  sendStatus(app, frame, 57000);
  sendCodex(app, 40, 84, true, 57000);
  sendClaude(app, 255, 60, true, 57000);
  save(display, "cached");

  frame.cpuTemperatureTenths = frame.gpuTemperatureTenths = macstatus::kMissingTemperature;
  frame.codexRemainingTenths = macstatus::kMissingCodexUsage;
  strcpy(frame.networkLocation, "--");
  sendStatus(app, frame, 58000);
  sendCodex(app, -1, -1, true, 58000);
  sendClaude(app, -1, -1, true, 58000);
  save(display, "missing");
}

// Offline clock, midnight roll-over, reconnect and timer invariants.
void renderOfflineStates() {
  TFT_eSPI display;
  MiniDisplayApp app(display);
  app.begin(0);
  app.tick(5000);
  require(app.offline() && !app.clock().valid(), "app.offline() && !app.clock().valid()");
  require(app.brightness() == MiniDisplayApp::kDefaultOfflineBrightness, "app.brightness() == MiniDisplayApp::kDefaultOfflineBrightness");
  save(display, "offline-unsynced");

  send(app, "MSC1,86399", 5000);
  app.tick(5000);
  save(display, "offline");
  const size_t before = display.writes;
  app.tick(5000);
  require(display.writes == before, "display.writes == before");
  app.tick(6000);
  require(app.clock().seconds() == 0, "app.clock().seconds() == 0");
  save(display, "midnight");

  StatusFrame frame = typicalFrame();
  sendStatus(app, frame, 6000);
  require(!app.offline(), "!app.offline()");
  save(display, "reconnected");

  // Auxiliary usage and clock frames must never count as a system heartbeat.
  sendClaude(app, 250, 700, false, 8000);
  send(app, "MSC1,10", 9000);
  require(app.claudeUsage().fiveHourTenths == 250, "app.claudeUsage().fiveHourTenths == 250");
  app.tick(10001);
  require(app.offline(), "app.offline()");

  sendCodex(app, 300, 600, false, 8000);
  require(app.codexUsage().fiveHourTenths == 300, "MSA2 is accepted while offline");

  // Quota values expire 15 s after their last MSA1/MSA2 frame even while live;
  // Codex then falls back to MSD4's weekly value.
  sendStatus(app, frame, 24000);
  app.tick(24000);
  require(!app.offline() && app.claudeUsage().fiveHourTenths == -1, "!app.offline() && app.claudeUsage().fiveHourTenths == -1");
  require(app.codexUsage().fiveHourTenths == -1 && app.codexUsage().weekTenths == frame.codexRemainingTenths,
          "expired MSA2 falls back to the MSD4 weekly value");

  // Timers keep working over the uint32_t millis() wrap.
  sendStatus(app, frame, UINT32_MAX - 1000);
  app.tick(2000);
  require(!app.offline(), "!app.offline()");
  app.tick(3500);
  require(app.offline(), "app.offline()");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: status-preview OUTPUT_DIRECTORY\n";
    return 2;
  }
  directory = argv[1];
  try {
    checkFonts();
    renderLiveStates();
    renderOfflineStates();
  } catch (const std::exception &error) {
    std::cerr << "status preview failed: " << error.what() << "\n";
    return 1;
  }
  std::cout << "11 real-code previews; bounds, overlap, clock, reconnect and timer checks passed\n";
  return 0;
}
