#include "minidisplay_app.h"

#include <Arduino.h>

namespace minidisplay {

void MiniDisplayApp::begin(uint32_t now) {
  pinMode(TFT_BL, OUTPUT);
  analogWriteRange(1023);
  applyBrightness(kDefaultDayBrightness);
  tft_.begin();
  tft_.invertDisplay(1);
  tft_.setRotation(0);
  offlinePage_.begin();
  status_.drawLayout();
  lastStatusAt_ = now;
}

void MiniDisplayApp::handleLine(char *line, uint32_t now) {
#ifdef ARDUINO
  if (macstatus::validAuxFrame(line, "$MSQ1*")) {
    Serial.printf(
        "MSQ1 offline=%u clock=%u date=%u weather=%u epoch=%u heap=%u claude5=%d claude7=%d "
        "claude_stale=%u codex5=%d codex7=%d codex_stale=%u\n",
        offline_, clock_.valid(), clock_.dateValid(), offlinePage_.hasWeather(), clock_.epoch(),
        ESP.getFreeHeap(), claude_.fiveHourTenths, claude_.weekTenths, claude_.stale,
        codexUsage().fiveHourTenths, codexUsage().weekTenths, codexUsage().stale);
    return;
  }
#endif
  macstatus::QuotaFrame quota;
  if (macstatus::parseClaudeUsageFrame(line, quota)) {
    claude_ = quota;
    hasClaude_ = true;
    lastClaudeAt_ = now;
    if (!offline_) status_.showClaude(claude_);
    return;
  }
  if (macstatus::parseCodexUsageFrame(line, quota)) {
    codex_ = quota;
    hasCodex_ = true;
    lastCodexAt_ = now;
    if (!offline_) status_.showCodex(codexUsage());
    return;
  }
  uint32_t clockValue = 0;
  if (macstatus::parseCalendarFrame(line, clockValue)) {
    clock_.syncEpoch(clockValue, now);
    return;
  }
  if (offlinePage_.acceptWeather(line)) {
#ifdef ARDUINO
    Serial.println("MSW1 OK");
#endif
    return;
  }
  if (macstatus::parseClockFrame(line, clockValue)) {
    clock_.sync(clockValue, now);
    return;  // Clock traffic alone must not keep a stale status page alive.
  }
  macstatus::StatusFrame frame;
  if (!macstatus::parseStatusFrame(line, frame)) return;
  lastStatusAt_ = now;
  showStatus(frame);
}

macstatus::QuotaFrame MiniDisplayApp::codexUsage() const {
  if (hasCodex_) return codex_;
  macstatus::QuotaFrame weekly;
  weekly.weekTenths = statusCodexWeekTenths_;
  weekly.stale = statusCodexStale_;
  return weekly;
}

void MiniDisplayApp::showStatus(const macstatus::StatusFrame &frame) {
  statusCodexWeekTenths_ = frame.codexRemainingTenths;
  statusCodexStale_ = frame.codexUsageStale;
  offlineBrightness_ = frame.offlineBrightnessPercent;
  applyBrightness(frame.brightnessPercent);
  if (offline_) {
    status_.drawLayout();
    offline_ = false;
  }
  status_.showFrame(frame);
  status_.showCodex(codexUsage());
  status_.showClaude(claude_);
  status_.showClock(clock_);
}

void MiniDisplayApp::tick(uint32_t now) {
  clock_.tick(now);
  if (hasClaude_ && now - lastClaudeAt_ > kQuotaExpiryMs) {
    hasClaude_ = false;
    claude_ = macstatus::QuotaFrame();
    if (!offline_) status_.showClaude(claude_);
  }
  if (hasCodex_ && now - lastCodexAt_ > kQuotaExpiryMs) {
    hasCodex_ = false;
    codex_ = macstatus::QuotaFrame();
    if (!offline_) status_.showCodex(codexUsage());
  }
  if (now - lastStatusAt_ <= kOfflineAfterMs) {
    if (!offline_) status_.showClock(clock_);
    return;
  }
  if (!offline_) {
#ifdef ARDUINO
    Serial.printf("MSD4 OFFLINE clock=%u date=%u weather=%u heap=%u\n", clock_.valid(),
                  clock_.dateValid(), offlinePage_.hasWeather(), ESP.getFreeHeap());
#endif
    offlinePage_.invalidate();
    offline_ = true;
  }
  offlinePage_.draw(clock_);
  applyBrightness(offlineBrightness_);
}

void MiniDisplayApp::applyBrightness(uint8_t percent) {
  if (percent == brightness_) return;
  const uint16_t pwm =
      static_cast<uint16_t>(1023U - (static_cast<uint32_t>(percent) * 1023U) / 100U);
  analogWrite(TFT_BL, pwm);
  brightness_ = percent;
}

}  // namespace minidisplay
