#include "status_screen.h"

#include "country_flags.h"
#include "display_theme.h"
#include "status_format.h"

#if defined(ARDUINO) && !defined(LOAD_FONT4)
#error "The status page draws its large values with TFT_eSPI font 4; add -D LOAD_FONT4=1."
#endif

namespace minidisplay {

using namespace theme;

namespace {

// Layout of the 240 x 240 page. Every text box stays inside its card and never
// overlaps another text box; tools/status_preview enforces both properties.
constexpr int16_t kHeaderMidY = 14;
constexpr Rect kHeaderLeft = {8, 2, 138, 27};
constexpr Rect kConnection = {148, 4, 84, 22};
constexpr int16_t kHeaderRuleY = 30;

constexpr Rect kSystemCard = {8, 36, 224, 79};
constexpr int16_t kColumnWidth = 92;
constexpr int16_t kCpuX = 16;
constexpr int16_t kMemoryX = 132;
constexpr int16_t kLoadLabelY = 43;
constexpr int16_t kLoadValueY = 54;
constexpr int16_t kLoadBarY = 82;
constexpr int16_t kBarHeight = 7;
constexpr int16_t kTemperatureMidY = 103;

constexpr int16_t kUsageCardY = 121;
constexpr int16_t kUsageCardWidth = 108;
constexpr int16_t kUsageCardHeight = 66;
constexpr int16_t kCodexCardX = 8;
constexpr int16_t kClaudeCardX = 124;
constexpr int16_t kUsageLabelY = 128;
constexpr int16_t kUsageValueY = 139;
constexpr int16_t kUsageBarY = 162;
constexpr int16_t kUsageBarHeight = 6;
constexpr int16_t kUsageDetailY = 173;

constexpr Rect kNetworkCard = {8, 193, 224, 39};
constexpr int16_t kNetworkLabelY = 198;
constexpr Rect kLocationArea = {14, 208, 64, 19};
constexpr Rect kDownloadArea = {86, 208, 67, 19};
constexpr Rect kUploadArea = {161, 208, 65, 19};
constexpr int16_t kCornerRadius = 6;

const char *const kWeekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

void fill(TFT_eSPI &tft, const Rect &area, uint16_t color) {
  tft.fillRect(area.x, area.y, area.width, area.height, color);
}

void frame(TFT_eSPI &tft, const Rect &area) {
  tft.drawRoundRect(area.x, area.y, area.width, area.height, kCornerRadius, kPanelBorder);
}

}  // namespace

void StatusScreen::setText(uint8_t font, uint8_t datum, uint16_t color) {
  tft_.setTextFont(font);
  tft_.setTextSize(1);
  tft_.setTextDatum(datum);
  tft_.setTextColor(color, kBackground);
}

void StatusScreen::drawLayout() {
  tft_.fillScreen(kBackground);
  hasFrame_ = false;
  headerMinute_ = kNoMinute;
  headerDate_ = false;

  drawHeader(nullptr);
  tft_.drawFastHLine(8, kHeaderRuleY, 224, kPanelBorder);
  drawConnection(false);

  frame(tft_, kSystemCard);
  tft_.drawFastVLine(120, 44, 63, kPanelBorder);
  drawLoad(kCpuX, "CPU", -1);
  drawLoad(kMemoryX, "MEMORY", -1);
  drawTemperature(kCpuX, "CPU", macstatus::kMissingTemperature);
  drawTemperature(kMemoryX, "GPU", macstatus::kMissingTemperature);

  frame(tft_, Rect{kCodexCardX, kUsageCardY, kUsageCardWidth, kUsageCardHeight});
  frame(tft_, Rect{kClaudeCardX, kUsageCardY, kUsageCardWidth, kUsageCardHeight});
  drawCodex(macstatus::kMissingCodexUsage, false);
  claude_ = macstatus::ClaudeUsageFrame();
  drawClaude(claude_);
  claudeDrawn_ = true;

  frame(tft_, kNetworkCard);
  setText(kFontSmall, TL_DATUM, kGreen);
  tft_.drawString("EXIT", 16, kNetworkLabelY);
  setText(kFontSmall, TL_DATUM, kBlue);
  tft_.drawString("DOWN", 94, kNetworkLabelY);
  setText(kFontSmall, TL_DATUM, kPurple);
  tft_.drawString("UP", 170, kNetworkLabelY);
  tft_.drawFastVLine(82, 201, 23, kPanelBorder);
  tft_.drawFastVLine(157, 201, 23, kPanelBorder);
  drawLocation("--", false);
  drawRates(false, 0, 0);
}

void StatusScreen::showFrame(const macstatus::StatusFrame &frame) {
  const bool all = !hasFrame_;
  if (all) drawConnection(true);
  if (all || frame.cpuTenths != shown_.cpuTenths) drawLoad(kCpuX, "CPU", frame.cpuTenths);
  if (all || frame.memoryTenths != shown_.memoryTenths)
    drawLoad(kMemoryX, "MEMORY", frame.memoryTenths);
  if (all || frame.cpuTemperatureTenths != shown_.cpuTemperatureTenths)
    drawTemperature(kCpuX, "CPU", frame.cpuTemperatureTenths);
  if (all || frame.gpuTemperatureTenths != shown_.gpuTemperatureTenths)
    drawTemperature(kMemoryX, "GPU", frame.gpuTemperatureTenths);
  if (all || frame.codexRemainingTenths != shown_.codexRemainingTenths ||
      frame.codexUsageStale != shown_.codexUsageStale)
    drawCodex(frame.codexRemainingTenths, frame.codexUsageStale);
  if (all || strcmp(frame.networkLocation, shown_.networkLocation) != 0 ||
      frame.networkLocationStale != shown_.networkLocationStale)
    drawLocation(frame.networkLocation, frame.networkLocationStale);
  if (all || frame.downloadBytesPerSecond != shown_.downloadBytesPerSecond ||
      frame.uploadBytesPerSecond != shown_.uploadBytesPerSecond)
    drawRates(true, frame.downloadBytesPerSecond, frame.uploadBytesPerSecond);
  shown_ = frame;
  hasFrame_ = true;
}

void StatusScreen::showClaude(const macstatus::ClaudeUsageFrame &usage) {
  if (claudeDrawn_ && usage == claude_) return;
  drawClaude(usage);
  claude_ = usage;
  claudeDrawn_ = true;
}

void StatusScreen::showClock(const macstatus::OfflineClock &clock) {
  const uint32_t minute = clock.valid() ? clock.days() * 1440UL + clock.seconds() / 60 : kNoMinute;
  if (minute == headerMinute_ && clock.dateValid() == headerDate_) return;
  drawHeader(clock.valid() ? &clock : nullptr);
  headerMinute_ = minute;
  headerDate_ = clock.dateValid();
}

void StatusScreen::drawHeader(const macstatus::OfflineClock *clock) {
  fill(tft_, kHeaderLeft, kBackground);
  if (clock == nullptr) {
    setText(kFontLabel, ML_DATUM, kText);
    tft_.drawString("MINIDISPLAY", 10, kHeaderMidY + 1);
    return;
  }
  const uint32_t seconds = clock->seconds();
  char text[12];
  snprintf(text, sizeof(text), "%02u:%02u", static_cast<unsigned>(seconds / 3600),
           static_cast<unsigned>(seconds / 60 % 60));
  setText(kFontValue, TL_DATUM, kText);
  const int16_t timeWidth = tft_.drawString(text, 10, 3);
  if (!clock->dateValid()) return;
  const macstatus::CivilDate date = macstatus::civilFromDays(clock->days());
  snprintf(text, sizeof(text), "%02u-%02u %s", static_cast<unsigned>(date.month),
           static_cast<unsigned>(date.day), kWeekdays[date.weekday]);
  setText(kFontSmall, ML_DATUM, kMuted);
  tft_.drawString(text, 10 + timeWidth + 8, kHeaderMidY);
}

void StatusScreen::drawConnection(bool live) {
  const uint16_t color = live ? kGreen : kYellow;
  fill(tft_, kConnection, kBackground);
  tft_.fillCircle(156, kHeaderMidY, 4, color);
  setText(kFontSmall, ML_DATUM, color);
  tft_.drawString(live ? "USB LIVE" : "WAITING", 166, kHeaderMidY);
}

void StatusScreen::drawProgressBar(int16_t x, int16_t y, int16_t width, int16_t height,
                                   int16_t tenths, uint16_t color) {
  const int16_t radius = height / 2;
  tft_.fillRoundRect(x, y, width, height, radius, kPanelBorder);
  if (tenths <= 0) return;
  const uint32_t clamped = tenths > 1000 ? 1000U : static_cast<uint32_t>(tenths);
  const int16_t filled = static_cast<int16_t>((static_cast<uint32_t>(width) * clamped) / 1000U);
  if (filled > 0) tft_.fillRoundRect(x, y, filled, height, radius, color);
}

void StatusScreen::drawLoad(int16_t x, const char *label, int16_t tenths) {
  tft_.fillRect(x, 41, kColumnWidth, 49, kBackground);
  setText(kFontSmall, TL_DATUM, kMuted);
  tft_.drawString(label, x, kLoadLabelY);
  char value[8];
  formatPercent(tenths, value, sizeof(value));
  setText(kFontValue, TL_DATUM, kText);
  tft_.drawString(value, x, kLoadValueY);
  drawProgressBar(x, kLoadBarY, kColumnWidth, kBarHeight, tenths,
                  tenths < 0 ? kMuted : loadColor(tenths));
}

void StatusScreen::drawTemperature(int16_t x, const char *label, int16_t tenths) {
  tft_.fillRect(x, 93, kColumnWidth, 19, kBackground);
  setText(kFontSmall, ML_DATUM, kMuted);
  tft_.drawString(label, x, kTemperatureMidY);
  const bool valid = tenths != macstatus::kMissingTemperature;
  char value[10];
  // Font 2 renders '`' as a degree sign.
  if (valid) snprintf(value, sizeof(value), "%d`C", static_cast<int>((tenths + 5) / 10));
  else snprintf(value, sizeof(value), "--`C");
  setText(kFontLabel, MR_DATUM, valid ? temperatureColor(tenths) : kMuted);
  tft_.drawString(value, x + kColumnWidth, kTemperatureMidY);
}

void StatusScreen::drawUsageCard(int16_t x, const char *label, int16_t remainingTenths,
                                 bool stale, uint16_t accent) {
  const bool valid = remainingTenths >= 0;
  const int16_t left = x + 8;
  tft_.fillRect(x + 6, 125, 96, 58, kBackground);
  setText(kFontSmall, TL_DATUM, accent);
  tft_.drawString(label, left, kUsageLabelY);
  // Freshness marker: accent when live, yellow when cached, grey when missing.
  tft_.fillCircle(x + 96, kUsageLabelY + 3, 2, valid ? (stale ? kYellow : accent) : kPanelBorder);

  char value[8];
  formatPercent(remainingTenths, value, sizeof(value));
  setText(kFontValue, TL_DATUM, valid && !stale ? kText : kMuted);
  tft_.drawString(value, left, kUsageValueY);
  const uint16_t barColor = !valid ? kMuted : stale ? kMuted : quotaColor(remainingTenths, accent);
  drawProgressBar(left, kUsageBarY, 92, kUsageBarHeight, remainingTenths, barColor);
}

void StatusScreen::drawCodex(int16_t remainingTenths, bool stale) {
  drawUsageCard(kCodexCardX, "CODEX", remainingTenths, stale, kCodex);
  setText(kFontSmall, TL_DATUM, kMuted);
  const char *detail = remainingTenths < 0 ? "WEEK --" : stale ? "WEEK CACHED" : "WEEK LEFT";
  tft_.drawString(detail, kCodexCardX + 8, kUsageDetailY);
}

void StatusScreen::drawClaude(const macstatus::ClaudeUsageFrame &usage) {
  drawUsageCard(kClaudeCardX, "CLAUDE 5H", usage.fiveHourTenths, usage.stale, kClaude);
  // Weekly window: "7D 42% LEFT", the percentage colored like the 5-hour bar.
  int16_t x = kClaudeCardX + 8;
  setText(kFontSmall, TL_DATUM, kMuted);
  x += tft_.drawString("7D ", x, kUsageDetailY);
  char value[8];
  formatPercent(usage.weekTenths, value, sizeof(value));
  const bool valid = usage.weekTenths >= 0;
  setText(kFontSmall, TL_DATUM,
          valid && !usage.stale ? quotaColor(usage.weekTenths, kText) : kMuted);
  x += tft_.drawString(value, x, kUsageDetailY);
  if (!valid) return;
  setText(kFontSmall, TL_DATUM, kMuted);
  tft_.drawString(usage.stale ? " OLD" : " LEFT", x, kUsageDetailY);
}

void StatusScreen::drawLocation(const char *location, bool stale) {
  char country[3];
  char detail[4];
  splitNetworkLocation(location, country, detail);
  fill(tft_, kLocationArea, kBackground);
  drawCountryFlag(tft_, 15, 211, country, stale);
  setText(kFontLabel, MC_DATUM, stale ? kMuted : kText);
  tft_.drawString(detail, 61, 217);
}

void StatusScreen::drawRates(bool valid, uint32_t download, uint32_t upload) {
  char downText[16];
  char upText[16];
  formatRate(download, downText, sizeof(downText));
  formatRate(upload, upText, sizeof(upText));
  fill(tft_, kDownloadArea, kBackground);
  fill(tft_, kUploadArea, kBackground);
  // Extreme rates fall back to the 6 px font rather than crossing a column rule.
  setText(kFontLabel, MC_DATUM, kText);
  if (tft_.textWidth(downText) > kDownloadArea.width - 2) tft_.setTextFont(kFontSmall);
  tft_.drawString(valid ? downText : "--", 119, 218);
  tft_.setTextFont(kFontLabel);
  if (tft_.textWidth(upText) > kUploadArea.width - 2) tft_.setTextFont(kFontSmall);
  tft_.drawString(valid ? upText : "--", 194, 218);
}

}  // namespace minidisplay
