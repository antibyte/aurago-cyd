// Host rasterizer for ui.cpp layout checks. Uses the firmware's actual font data.
// Circles/arcs approximate TFT anti-aliasing; this is not a hardware emulator.
#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>
#define PROGMEM
#define SCREEN_W 320
#define SCREEN_H 240
#define TFT_BLACK 0
#define TL_DATUM 0
#define TC_DATUM 1
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif
static tm *preview_localtime(const time_t *, tm *out) { *out = {}; out->tm_hour = 21; out->tm_min = 4; return out; }
struct GFXglyph { uint16_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; };
struct GFXfont { uint8_t *bitmap; GFXglyph *glyph; uint16_t first, last; uint8_t yAdvance; };
#include "Fonts/GFXFF/FreeSansBold18pt7b.h"
#include "Fonts/Font16.c"
#include "Fonts/Font32rle.c"
#include "Fonts/glcdfont.c"
template<typename T> T constrain(T v, T lo, T hi) { return std::max(lo, std::min(v, hi)); }
uint32_t millis() { return 120000; }
void yield() {}

struct PreviewTFT {
  uint16_t pixels[320 * 240]{};
  uint16_t fg = 0xffff, bg = 0;
  const GFXfont *freefont = nullptr;
  int font = 1, datum = 0;
  int origin_y = 0, clip_h = 240;
  int native_w = 320, native_h = 240;
  std::string last_text;
  int last_width = 0;
  void setFreeFont(const GFXfont *f) { freefont = f; font = 1; }
  void setTextFont(int f) { font = f; }
  void setTextDatum(int d) { datum = d; }
  void setTextPadding(int) {}
  void setTextColor(uint16_t f) { fg = f; bg = f; }
  void setTextColor(uint16_t f, uint16_t b) { fg = f; bg = b; }
  void drawPixel(int x, int y, uint16_t c) { y += origin_y; if (x >= 0 && x < 320 && y >= 0 && y < clip_h) pixels[y * 320 + x] = c; }
  void setViewport(int, int y, int, int) { origin_y = y; }
  void resetViewport() { origin_y = 0; }
  void fillRect(int x, int y, int w, int h, uint16_t c) {
    for (int yy = std::max(0, y); yy < std::min(240, y + h); yy++)
      for (int xx = std::max(0, x); xx < std::min(320, x + w); xx++) drawPixel(xx, yy, c);
  }
  void fillScreen(uint16_t c) { fillRect(0, 0, native_w, native_h, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawLine(int x, int y, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x), sx = x < x1 ? 1 : -1, dy = -abs(y1 - y), sy = y < y1 ? 1 : -1, e = dx + dy;
    for (;;) { drawPixel(x, y, c); if (x == x1 && y == y1) break;
      int e2 = e * 2; if (e2 >= dy) { e += dy; x += sx; } if (e2 <= dx) { e += dx; y += sy; }
    }
  }
  void fillCircle(int x, int y, int r, uint16_t c) {
    for (int yy = -r; yy <= r; yy++) for (int xx = -r; xx <= r; xx++)
      if (xx * xx + yy * yy <= r * r) drawPixel(x + xx, y + yy, c);
  }
  void drawCircle(int x, int y, int r, uint16_t c) {
    for (int i = 0; i < 720; i++) drawPixel(x + int(std::round(r * cos(i * 3.14159265 / 360))), y + int(std::round(r * sin(i * 3.14159265 / 360))), c);
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    fillRect(x + r, y, w - 2 * r, h, c); fillRect(x, y + r, w, h - 2 * r, c);
    for (int cx : {x + r, x + w - r - 1}) for (int cy : {y + r, y + h - r - 1}) fillCircle(cx, cy, r, c);
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    drawFastHLine(x + r, y, w - 2 * r, c); drawFastHLine(x + r, y + h - 1, w - 2 * r, c);
    drawFastVLine(x, y + r, h - 2 * r, c); drawFastVLine(x + w - 1, y + r, h - 2 * r, c);
    for (int i = 0; i <= 90; i++) { int a = int(std::round(r * cos(i * 3.14159265 / 180))), b = int(std::round(r * sin(i * 3.14159265 / 180)));
      drawPixel(x + r - a, y + r - b, c); drawPixel(x + w - r - 1 + a, y + r - b, c);
      drawPixel(x + r - a, y + h - r - 1 + b, c); drawPixel(x + w - r - 1 + a, y + h - r - 1 + b, c);
    }
  }
  void drawArc(int x, int y, int r, int ir, int start, int end, uint16_t c, uint16_t, bool) {
    for (int yy = -r; yy <= r; yy++) for (int xx = -r; xx <= r; xx++) {
      float d = std::sqrt(float(xx * xx + yy * yy));
      float a = std::atan2(float(-xx), float(yy)) * 180 / 3.14159265f; if (a < 0) a += 360;
      if (d <= r && d >= ir && a >= start && a <= end) drawPixel(x + xx, y + yy, c);
    }
  }
  int advance(unsigned char c, int f) {
    if (c < 32 || c > 126) c = '?';
    if (freefont) return freefont->glyph[c - 32].xAdvance;
    return f == 2 ? widtbl_f16[c - 32] : f == 4 ? widtbl_f32[c - 32] : 6;
  }
  int textWidth(const char *s, int f) {
    int w = 0; for (size_t i = 0; s[i]; i++) w += advance(s[i], f); return w;
  }
  int textWidth(const char *s) { return textWidth(s, font); }
  void glyph(unsigned char c, int x, int y, int f) {
    if (c < 32 || c > 126) c = '?';
    int w = advance(c, f), h = f == 2 ? 16 : f == 4 ? 26 : 8;
    if (freefont) {
      auto g = freefont->glyph[c - 32]; int ascent = 0;
      for (int i = 0; i < 95; i++) ascent = std::max(ascent, -int(freefont->glyph[i].yOffset));
      for (int i = 0; i < g.width * g.height; i++)
        if (freefont->bitmap[g.bitmapOffset + i / 8] & (0x80 >> (i % 8))) drawPixel(x + g.xOffset + i % g.width, y + ascent + g.yOffset + i / g.width, fg);
    } else {
      if (bg != fg) fillRect(x, y, w, h, bg);
      if (f == 2) {
        int stride = (w + 6) / 8;
        for (int yy = 0; yy < 16; yy++) for (int xx = 0; xx < w - 1; xx++)
          if (chrtbl_f16[c - 32][yy * stride + xx / 8] & (0x80 >> (xx % 8))) drawPixel(x + xx, y + yy, fg);
      } else if (f == 4) {
        const auto *data = chrtbl_f32[c - 32]; int pos = 0;
        while (pos < w * h) { int run = (*data & 127) + 1; bool on = *data++ & 128;
          for (int i = 0; i < run && pos < w * h; i++, pos++) if (on) drawPixel(x + pos % w, y + pos / w, fg);
        }
      } else {
        for (int xx = 0; xx < 5; xx++) for (int yy = 0; yy < 8; yy++)
          if (::font[c * 5 + xx] & (1 << yy)) drawPixel(x + xx, y + yy, fg);
      }
    }
  }
  int drawString(const char *s, int x, int y, int f) {
    int w = textWidth(s, f); if (datum == TC_DATUM) x -= w / 2;
    // Fail on clipped text, even if the rasterizer could silently crop it.
    assert(x >= 0 && x + w <= 320 && y >= 0 && y + (f == 4 ? 26 : f == 2 ? 16 : 8) <= 240);
    last_text = s; last_width = w;
    for (size_t i = 0; s[i]; i++) { glyph(s[i], x, y, f); x += advance(s[i], f); } return w;
  }
  int drawString(const char *s, int x, int y) { return drawString(s, x, y, font); }
  int drawCentreString(const char *s, int x, int y, int f) { int old = datum; datum = TL_DATUM; int w = drawString(s, x - textWidth(s, f) / 2, y, f); datum = old; return w; }
  int drawRightString(const char *s, int x, int y, int f) { int old = datum; datum = TL_DATUM; int w = drawString(s, x - textWidth(s, f), y, f); datum = old; return w; }
  void save(const std::string &name) {
    std::ofstream out(name + ".ppm", std::ios::binary); out << "P6\n320 240\n255\n";
    for (uint16_t p : pixels) { unsigned char v[] = {static_cast<unsigned char>(((p >> 11) & 31) * 255 / 31), static_cast<unsigned char>(((p >> 5) & 63) * 255 / 63), static_cast<unsigned char>((p & 31) * 255 / 31)}; out.write(reinterpret_cast<char *>(v), 3); }
  }
};
static PreviewTFT tft;
using TFT_eSPI = PreviewTFT;
struct TFT_eSprite : PreviewTFT {
  PreviewTFT *target;
  bool ready = false;
  explicit TFT_eSprite(PreviewTFT *t) : target(t) {}
  void setColorDepth(int) {}
  void createSprite(int, int h) { clip_h = h; native_w = 240; native_h = 320; ready = true; }
  bool created() { return ready; }
  void pushSprite(int x, int y) {
    for (int row = 0; row < clip_h; row++) for (int col = 0; col < 320; col++) target->drawPixel(x + col, y + row, pixels[row * 320 + col]);
  }
};
