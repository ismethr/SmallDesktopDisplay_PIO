#pragma once
// Host raster adapter for the real status-screen drawing code. It renders the
// exact bundled TFT_eSPI font data (GLCD font 1, bitmap font 2 and RLE font 4)
// and fills each text cell's background like TFT_eSPI does when the text and
// background colors differ. Hardware inversion and panel color order still
// need a real panel.
#include "Arduino.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "Font16.c"
#include "Font32rle.c"
#include "glcdfont.c"

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5

// Font32rle.h, which TFT_eSPI uses for this value, re-includes the .c file.
constexpr int kFont4Height = 26;

class TFT_eSPI {
 public:
  static constexpr int kSize = 240;
  std::array<uint16_t, kSize * kSize> pixels{};
  size_t writes = 0;

  void begin() {}
  void invertDisplay(int) {}
  void setRotation(int) {}
  void setTextDatum(int value) { datum_ = value; }
  void setTextFont(int value) {
    if (value != 1 && value != 2 && value != 4) throw std::runtime_error("font is not loaded");
    font_ = value;
  }
  void setTextSize(int value) { scale_ = value; }
  void setTextColor(uint16_t foreground, uint16_t background) {
    foreground_ = foreground;
    background_ = background;
  }

  void drawPixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= kSize || y < 0 || y >= kSize) throw std::runtime_error("pixel outside 240x240 screen");
    pixels[y * kSize + x] = color;
    ++writes;
  }
  void fillRect(int x, int y, int w, int h, uint16_t color) {
    // A fill that fully covers a label erases it from the overlap bookkeeping.
    labels_.erase(std::remove_if(labels_.begin(), labels_.end(), [&](const Label &label) {
      return x <= label.x && y <= label.y && x + w >= label.x + label.w && y + h >= label.y + label.h;
    }), labels_.end());
    for (int j = y; j < y + h; ++j)
      for (int i = x; i < x + w; ++i) drawPixel(i, j, color);
  }
  void fillScreen(uint16_t color) {
    labels_.clear();
    fillRect(0, 0, kSize, kSize, color);
  }
  void drawFastHLine(int x, int y, int w, uint16_t c) { for (int i = 0; i < w; ++i) drawPixel(x + i, y, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { for (int i = 0; i < h; ++i) drawPixel(x, y + i, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) {
    drawFastHLine(x, y, w, c);
    drawFastHLine(x, y + h - 1, w, c);
    drawFastVLine(x, y, h, c);
    drawFastVLine(x + w - 1, y, h, c);
  }
  void drawLine(int x, int y, int endX, int endY, uint16_t c) {
    int dx = std::abs(endX - x), sx = x < endX ? 1 : -1, dy = -std::abs(endY - y), sy = y < endY ? 1 : -1;
    for (int error = dx + dy;;) {
      drawPixel(x, y, c);
      if (x == endX && y == endY) break;
      const int e = 2 * error;
      if (e >= dy) { error += dy; x += sx; }
      if (e <= dx) { error += dx; y += sy; }
    }
  }
  void fillCircle(int x, int y, int r, uint16_t c) {
    for (int j = -r; j <= r; ++j)
      for (int i = -r; i <= r; ++i)
        if (i * i + j * j <= r * r) drawPixel(x + i, y + j, c);
  }
  void drawCircle(int x, int y, int r, uint16_t c) {
    for (int j = -r; j <= r; ++j)
      for (int i = -r; i <= r; ++i)
        if (std::abs(std::sqrt(double(i * i + j * j)) - r) < .6) drawPixel(x + i, y + j, c);
  }
  void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c) {
    auto edge = [](int a, int b, int d, int e, int x, int y) { return (x - a) * (e - b) - (y - b) * (d - a); };
    for (int y = std::min({y1, y2, y3}); y <= std::max({y1, y2, y3}); ++y)
      for (int x = std::min({x1, x2, x3}); x <= std::max({x1, x2, x3}); ++x) {
        const int a = edge(x1, y1, x2, y2, x, y), b = edge(x2, y2, x3, y3, x, y), d = edge(x3, y3, x1, y1, x, y);
        if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) drawPixel(x, y, c);
      }
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    r = std::min({r, w / 2, h / 2});
    for (int j = 0; j < h; ++j)
      for (int i = 0; i < w; ++i) {
        const int dx = std::max({r - i, 0, i - (w - r - 1)}), dy = std::max({r - j, 0, j - (h - r - 1)});
        if (dx * dx + dy * dy <= r * r) drawPixel(x + i, y + j, c);
      }
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    fillRoundRect(x, y, w, h, r, c);
    fillRoundRect(x + 1, y + 1, w - 2, h - 2, std::max(0, r - 1), TFT_BLACK);
  }

  int fontHeight() const { return cellHeight(font_) * scale_; }
  int textWidth(const char *text) const {
    int width = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) width += advance(*p);
    return width * scale_;
  }
  int drawString(const char *text, int x, int y) {
    const int width = textWidth(text), height = fontHeight();
    if (datum_ % 3 == 1) x -= width / 2;
    else if (datum_ % 3 == 2) x -= width;
    if (datum_ >= 3) y -= height / 2;
    Label label{x, y, width, height, text};
    if (x < 0 || y < 0 || x + width > kSize || y + height > kSize)
      throw std::runtime_error("text outside screen: " + label.text);
    for (const auto &other : labels_)
      if (intersects(label, other)) throw std::runtime_error("overlapping text: " + label.text + " / " + other.text);
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
      if (*p < 32 || *p > 127) throw std::runtime_error("glyph outside the built-in fonts: " + label.text);
      if (foreground_ != background_) fillCell(x, y, advance(*p) * scale_, height, background_);
      drawGlyph(*p, x, y);
      x += advance(*p) * scale_;
    }
    labels_.push_back(label);
    return width;
  }

  // Decodes every font 4 glyph and checks each RLE stream covers its cell exactly.
  static void verifyRleFont() {
    for (int index = 0; index < 96; ++index) {
      const int total = widtbl_f32[index] * kFont4Height;
      int covered = 0;
      for (const unsigned char *data = chrtbl_f32[index]; covered < total; ++data) covered += (*data & 0x7F) + 1;
      if (covered != total) throw std::runtime_error("font 4 RLE glyph overruns its cell");
    }
  }

  void save(const std::string &path) const {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n240 240\n255\n";
    for (uint16_t c : pixels) {
      const char rgb[] = {char(((c >> 11) & 31) * 255 / 31), char(((c >> 5) & 63) * 255 / 63), char((c & 31) * 255 / 31)};
      out.write(rgb, 3);
    }
    if (!out) throw std::runtime_error("cannot save preview");
  }

 private:
  struct Label { int x, y, w, h; std::string text; };

  static bool intersects(const Label &a, const Label &b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
  }
  static int cellHeight(int font) { return font == 4 ? kFont4Height : font == 2 ? 16 : 8; }
  int advance(unsigned char c) const {
    if (font_ == 1) return 6;
    if (c < 32 || c > 127) return 0;
    return font_ == 4 ? widtbl_f32[c - 32] : widtbl_f16[c - 32];
  }
  void fillCell(int x, int y, int w, int h, uint16_t color) {
    for (int j = y; j < y + h; ++j)
      for (int i = x; i < x + w; ++i) drawPixel(i, j, color);
  }
  void plot(int x, int y, int column, int row) {
    for (int a = 0; a < scale_; ++a)
      for (int b = 0; b < scale_; ++b) drawPixel(x + column * scale_ + b, y + row * scale_ + a, foreground_);
  }
  void drawGlyph(unsigned char c, int x, int y) {
    const int w = advance(c);
    if (font_ == 4) {
      // Bit 7 set: run of (n & 0x7F) + 1 ink pixels, otherwise n + 1 background pixels.
      int pixel = 0;
      for (const unsigned char *data = chrtbl_f32[c - 32]; pixel < w * kFont4Height; ++data) {
        const int run = (*data & 0x7F) + 1;
        for (int i = 0; i < run; ++i, ++pixel)
          if (*data & 0x80) plot(x, y, pixel % w, pixel / w);
      }
    } else if (font_ == 2) {
      // Font 2's advance includes a trailing pixel: TFT_eSPI deliberately
      // uses +6, not +7, when deriving bitmap row bytes (notably for '%').
      const int rowBytes = (w + 6) / 8;
      for (int j = 0; j < 16; ++j)
        for (int i = 0; i < w && i < rowBytes * 8; ++i)
          if (chrtbl_f16[c - 32][j * rowBytes + i / 8] & (0x80 >> (i % 8))) plot(x, y, i, j);
    } else {
      for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 5; ++i)
          if (font[c * 5 + i] & (1 << j)) plot(x, y, i, j);
    }
  }

  int font_ = 1, scale_ = 1, datum_ = TL_DATUM;
  uint16_t foreground_ = TFT_WHITE, background_ = TFT_WHITE;
  std::vector<Label> labels_;
};
