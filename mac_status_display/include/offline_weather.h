#pragma once

#include <ArduinoJson.h>
#include <TimeLib.h>
#include <TJpg_Decoder.h>
#include "../../src/font/ZdyLwFont_20.h"
#include "../../src/font/font_td_20.h"
#include "../../src/img/temperature.h"
#include "../../src/img/humidity.h"
#include "../../src/Animate/Animate.h"
#include "../../src/weatherNum/weatherNum.h"
#include "../../src/core/DisplayLogic.h"

// Only cached USB data is used; this firmware never starts Wi-Fi.
class OfflineWeatherPage {
 public:
  explicit OfflineWeatherPage(TFT_eSPI &screen) : tft(screen), sprite(&screen) {}
  bool accept(const char *line) {
    if (!macstatus::validAuxFrame(line, "$MSW1,")) return false;
    StaticJsonDocument<1536> doc;
    if (deserializeJson(doc, line + 6, strrchr(line, '*') - line - 6)) return false;
    for (const char *key : {"city", "weather", "wind", "at"}) {
      if (!doc[key].is<const char *>()) return false;
      const char *text = doc[key];
      if (strlen(text) > 100) return false;
      for (; *text; ++text) if (static_cast<unsigned char>(*text) < 32) return false;
    }
    for (const char *key : {"humidity", "code", "aqi", "low", "high"})
      if (!doc[key].is<int>()) return false;
    if (!doc["temp"].is<float>()) return false;
    float value = doc["temp"];
    if (!std::isfinite(value) || value < -60 || value > 60 ||
        doc["humidity"].as<int>() < 0 || doc["humidity"].as<int>() > 100 ||
        doc["code"].as<int>() < 0 || doc["code"].as<int>() > 999 ||
        doc["aqi"].as<int>() < -1 || doc["aqi"].as<int>() > 999 ||
        doc["low"].as<int>() < -60 || doc["high"].as<int>() > 60 ||
        doc["low"].as<int>() > doc["high"].as<int>()) return false;
    city = doc["city"].as<String>();
    weather = doc["weather"].as<String>();
    wind = doc["wind"].as<String>();
    updated = doc["at"].as<String>();
    temp = value; hum = doc["humidity"]; code = doc["code"]; aqi = doc["aqi"];
    low = doc["low"]; high = doc["high"];
    valid = dirty = true;
    return true;
  }
  bool hasWeather() const { return valid; }
  void draw(const macstatus::OfflineClock &clock, bool refresh) {
    const uint32_t now = millis();
    refresh = refresh || dirty;
    if (refresh) {
      text(city, 5, 15, 70, 30);
      String quality = "AQI --";
      uint16_t color = 0x528A;
      if (aqi >= 0) {
        if (aqi <= 50) { quality = "优"; color = tft.color565(156, 202, 127); }
        else if (aqi <= 100) { quality = "良"; color = tft.color565(247, 219, 100); }
        else if (aqi <= 150) { quality = "轻度"; color = tft.color565(242, 159, 57); }
        else if (aqi <= 200) { quality = "中度"; color = tft.color565(186, 55, 121); }
        else if (aqi <= 300) { quality = "重度"; color = tft.color565(136, 11, 32); }
        else { quality = "严重"; color = tft.color565(88, 6, 20); }
        quality += " " + String(aqi);
      }
      text(quality, 80, 18, 85, 24, ZdyLwFont_20, color, TFT_BLACK, true);
      WeatherNum().draw(170, 15, code);
      TJpgDec.drawJpg(15, 183, temperature, sizeof(temperature));
      TJpgDec.drawJpg(15, 213, humidity, sizeof(humidity));
      text(valid ? String(temp, temp == static_cast<int>(temp) ? 0 : 1) + "℃" : "--", 100, 184, 58, 24);
      text(valid ? String(hum) + "%" : "--", 100, 214, 58, 24);
      int width = valid ? sdd::temperatureBarWidth(static_cast<int>(temp)) : 0;
      uint16_t tempColor = width < 10 ? 0x00FF : width < 28 ? 0x0AFF : width < 34 ? 0x0F0F : width < 41 ? 0xFF0F : 0xF00F;
      uint16_t humColor = hum > 90 ? 0x00FF : hum > 70 ? 0x0AFF : hum > 40 ? 0x0F0F : hum > 20 ? 0xFF0F : 0xF00F;
      tft.fillRect(45, 192, 52, 6, TFT_BLACK);
      tft.drawRoundRect(45, 192, 52, 6, 3, TFT_WHITE);
      tft.fillRoundRect(46, 193, width, 4, 2, tempColor);
      tft.fillRect(45, 222, 52, 6, TFT_BLACK);
      tft.drawRoundRect(45, 222, 52, 6, 3, TFT_WHITE);
      tft.fillRoundRect(46, 223, valid ? hum / 2 : 0, 4, 2, humColor);
      dirty = false;
    }
    // Repaint only the changing bands; animation and clock never clear each other.
    uint32_t phase = now / 80;
    if (refresh || phase != bannerPhase) {
      String banner;
      switch ((now / 5000) % 5) {
        case 0: banner = valid ? "天气 " + weather : "WEATHER WAIT"; break;
        case 1: banner = valid ? "风向 " + (wind.length() ? wind : "--") : "WEATHER WAIT"; break;
        case 2: banner = valid ? "最低温度 " + String(low) + "℃" : "WEATHER WAIT"; break;
        case 3: banner = valid ? "最高温度 " + String(high) + "℃" : "WEATHER WAIT"; break;
        default: banner = valid ? "天气时间 " + updated : "WEATHER WAIT"; break;
      }
      text(banner, 5, 45, 150, 30, ZdyLwFont_20, TFT_BLACK, TFT_WHITE, false, (now % 5000) / 80);
      bannerPhase = phase;
    }
    const uint32_t day = clock.dateValid() ? clock.epoch() / 86400 : 0;
    if (refresh || day != dateDay) {
      String date = "WAIT";
      if (clock.dateValid()) {
        const char *week[] = {"日", "一", "二", "三", "四", "五", "六"};
        time_t epoch = clock.epoch();
        date = String(month(epoch)) + "月" + String(::day(epoch)) + "日 周" + week[weekday(epoch) - 1];
      }
      text(date, 5, 150, 150, 30, font_td_20);
      dateDay = day;
    }
    if (refresh || now - animatedAt >= 100) {
      const uint8_t *frame; uint32_t size;
      imgAnim(&frame, &size);
      if (frame && size) TJpgDec.drawJpg(160, 160, frame, size);
      animatedAt = now;
    }
  }
 private:
  TFT_eSPI &tft;
  TFT_eSprite sprite;
  bool valid = false, dirty = false;
  String city = "天气", weather, wind, updated;
  float temp = 0;
  int hum = 0, code = 99, aqi = -1, low = 0, high = 0;
  uint32_t animatedAt = 0, bannerPhase = UINT32_MAX, dateDay = UINT32_MAX;
  void text(const String &value, int x, int y, int width, int height,
            const uint8_t *font = ZdyLwFont_20, uint16_t background = TFT_BLACK,
            uint16_t foreground = TFT_WHITE, bool rounded = false, uint32_t scroll = 0) {
    sprite.setColorDepth(8);
    sprite.loadFont(font);
    if (sprite.createSprite(width, height)) {
      sprite.fillSprite(TFT_BLACK);
      if (rounded) sprite.fillRoundRect(0, 0, width, height, 4, background);
      sprite.setTextColor(foreground, background);
      String supported;
      uint16_t index = 0, length = value.length();
      while (index < length) {
        uint16_t start = index, glyph;
        uint16_t cp = sprite.decodeUTF8(reinterpret_cast<uint8_t *>(const_cast<char *>(value.c_str())), &index, length - start);
        supported += cp == ' ' || sprite.getUnicodeIndex(cp, &glyph) ? value.substring(start, index) : "-";
      }
      int textWidth = sprite.textWidth(supported);
      sprite.setTextDatum(MC_DATUM);
      int center = width / 2;
      if (textWidth > width) center = textWidth / 2 - static_cast<int>(scroll % (textWidth - width + 30));
      sprite.drawString(supported, center, height / 2 + 1);
      sprite.pushSprite(x, y);
      sprite.deleteSprite();
    }
    sprite.unloadFont();
  }
};
