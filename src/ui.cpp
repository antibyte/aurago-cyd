#include "ui.h"
#include "hardware.h"

#include <Arduino.h>
#include <qrcode.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

// Compose RGB565 bands before sending them to the TFT: no clear-then-redraw flash.
// ponytail: six draw passes save 128 KB versus a full framebuffer; use DMA if profiling calls for it.
static TFT_eSprite band(&tft);
static TFT_eSPI *surface = &tft;
static constexpr int BAND_H = 40;

template <typename Draw>
static void paint_frame(Draw draw) {
  if (!band.created()) { draw(); return; }
  surface = &band;
  for (int y = 0; y < SCREEN_H; y += BAND_H) {
    band.setViewport(0, -y, SCREEN_W, SCREEN_H);
    draw();
    band.resetViewport();
    band.pushSprite(0, y);
    yield();
  }
  surface = &tft;
}

static constexpr uint16_t rgb(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}

static uint16_t COL_BG, COL_PANEL, COL_LINE, COL_TEXT, COL_MUTED;
static uint16_t COL_ACCENT, COL_GOOD, COL_WARN, COL_BAD, COL_BAR_BG;
static uint16_t COL_INK, COL_HERO, COL_GLOW;
static bool g_dark = true;

#define HIST_N 48
static float hist_cpu[HIST_N];
static float hist_mem[HIST_N];
static float hist_disk[HIST_N];
static uint8_t hist_i = 0;
static uint8_t hist_n = 0;
static char g_ip[20];

void ui_set_dark(bool dark) {
  g_dark = dark;
  if (dark) {
    COL_BG = rgb(0x10191E);
    COL_PANEL = rgb(0x1B292F);
    COL_LINE = rgb(0x30434A);
    COL_TEXT = rgb(0xEFF5F1);
    COL_MUTED = rgb(0xA4B8BC);
    COL_ACCENT = rgb(0x9DE8CC);
    COL_GOOD = rgb(0x9DE8CC);
    COL_WARN = rgb(0xF4C17A);
    COL_BAD = rgb(0xFF9188);
    COL_BAR_BG = rgb(0x293B42);
    COL_HERO = rgb(0x213E40);
    COL_GLOW = rgb(0x305858);
    COL_INK = rgb(0x101E20);
  } else {
    COL_BG = rgb(0xE7EFEB);
    COL_PANEL = rgb(0xF8FCF9);
    COL_LINE = rgb(0xBACEC7);
    COL_TEXT = rgb(0x152F2D);
    COL_MUTED = rgb(0x4D6862);
    COL_ACCENT = rgb(0x146651);
    COL_GOOD = rgb(0x146651);
    COL_WARN = rgb(0x865008);
    COL_BAD = rgb(0xB13232);
    COL_BAR_BG = rgb(0xD0DFD9);
    COL_HERO = rgb(0xD5E9DE);
    COL_GLOW = rgb(0xB4D4C5);
    COL_INK = rgb(0xFFFFFF);
  }
}

void ui_note_metrics(float cpu, float mem, float disk) {
  hist_cpu[hist_i] = cpu;
  hist_mem[hist_i] = mem;
  hist_disk[hist_i] = disk;
  hist_i = static_cast<uint8_t>((hist_i + 1) % HIST_N);
  if (hist_n < HIST_N) {
    hist_n++;
  }
}

void ui_set_link_info(const char *ip) {
  copy_trunc(g_ip, sizeof(g_ip), ip ? ip : "");
}

uint8_t ui_page_from_name(const char *name) {
  if (name == nullptr || name[0] == '\0') {
    return 0;
  }
  if (strcasecmp(name, "load") == 0) {
    return 1;
  }
  if (strcasecmp(name, "work") == 0) {
    return 2;
  }
  if (strcasecmp(name, "alerts") == 0) {
    return 3;
  }
  if (strcasecmp(name, "mesh") == 0) {
    return 4;
  }
  return 0;
}

static const char *page_title(uint8_t page) {
  switch (page) {
    case 1:
      return "Load";
    case 2:
      return "Work";
    case 3:
      return "Alerts";
    case 4:
      return "Mesh";
    default:
      return "Home";
  }
}

static void use_bitmap(uint8_t font) {
  surface->setFreeFont(nullptr);
  surface->setTextFont(font);
  surface->setTextDatum(TL_DATUM);
  surface->setTextPadding(0);
}

static void use_display() {
  surface->setFreeFont(&FreeSansBold18pt7b);
  surface->setTextDatum(TL_DATUM);
  surface->setTextPadding(0);
}

static void clock_text(char *out, size_t cap) {
  time_t now = time(nullptr);
  if (now < 1600000000) {
    snprintf(out, cap, "--:--");
    return;
  }
  struct tm t;
  localtime_r(&now, &t);
  snprintf(out, cap, "%02d:%02d", t.tm_hour, t.tm_min);
}

static void format_uptime(char *out, size_t cap, uint32_t secs) {
  uint32_t d = secs / 86400;
  uint32_t h = (secs % 86400) / 3600;
  uint32_t m = (secs % 3600) / 60;
  if (d > 0) {
    snprintf(out, cap, "%ud %02uh", d, h);
  } else if (h > 0) {
    snprintf(out, cap, "%uh %02um", h, m);
  } else {
    snprintf(out, cap, "%um", m);
  }
}

static void format_last(char *out, size_t cap, float hours) {
  if (hours < 0) {
    snprintf(out, cap, "never");
    return;
  }
  if (hours < 1.0f) {
    int mins = static_cast<int>(hours * 60.0f + 0.5f);
    if (mins < 1) {
      snprintf(out, cap, "now");
    } else {
      snprintf(out, cap, "%dm", mins);
    }
    return;
  }
  if (hours < 48.0f) {
    snprintf(out, cap, "%.0fh", hours);
    return;
  }
  snprintf(out, cap, "%.0fd", hours / 24.0f);
}

static uint16_t bar_color(float pct) {
  if (pct >= 90.0f) {
    return COL_BAD;
  }
  if (pct >= 70.0f) {
    return COL_WARN;
  }
  return COL_GOOD;
}

static void draw_wifi(int x, int y, int8_t rssi, bool online) {
  uint16_t col = online ? COL_ACCENT : COL_MUTED;
  int bars = 1;
  if (rssi > -55) {
    bars = 4;
  } else if (rssi > -65) {
    bars = 3;
  } else if (rssi > -75) {
    bars = 2;
  }
  if (!online) {
    bars = 1;
    col = COL_BAD;
  }
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 3;
    int bx = x + i * 5;
    int by = y + 12 - h;
    uint16_t c = i < bars ? col : COL_LINE;
    surface->fillRect(bx, by, 3, h, c);
  }
}

static void draw_card(int x, int y, int w, int h) {
  surface->fillRoundRect(x, y, w, h, 9, COL_PANEL);
  surface->drawFastHLine(x + 9, y, w - 18, COL_LINE);
}

// Pixel-aligned 16px icons: identical strokes, no bitmap storage or decoding.
static void draw_icon(uint8_t icon, int x, int y, uint16_t col) {
  switch (icon) {
    case 0: // Home
      surface->drawLine(x + 1, y + 7, x + 8, y + 1, col);
      surface->drawLine(x + 8, y + 1, x + 15, y + 7, col);
      surface->drawRoundRect(x + 3, y + 7, 11, 9, 2, col);
      surface->drawFastVLine(x + 8, y + 11, 5, col);
      break;
    case 1: // Load
      surface->fillRoundRect(x + 1, y + 10, 3, 6, 1, col);
      surface->fillRoundRect(x + 7, y + 3, 3, 13, 1, col);
      surface->fillRoundRect(x + 13, y + 7, 3, 9, 1, col);
      break;
    case 2: // Work
      surface->drawRoundRect(x + 1, y + 5, 15, 11, 2, col);
      surface->drawRoundRect(x + 5, y + 1, 7, 5, 1, col);
      surface->drawFastHLine(x + 2, y + 10, 13, col);
      surface->fillRect(x + 7, y + 9, 3, 3, col);
      break;
    case 3: // Alerts
      surface->drawRoundRect(x + 3, y + 2, 11, 11, 5, col);
      surface->drawFastHLine(x + 1, y + 13, 15, col);
      surface->fillCircle(x + 8, y + 16, 1, col);
      break;
    case 4: // Mesh
      surface->drawLine(x + 3, y + 4, x + 13, y + 7, col);
      surface->drawLine(x + 3, y + 4, x + 6, y + 14, col);
      surface->drawLine(x + 6, y + 14, x + 13, y + 7, col);
      surface->fillCircle(x + 3, y + 4, 3, col);
      surface->fillCircle(x + 13, y + 7, 3, col);
      surface->fillCircle(x + 6, y + 14, 3, col);
      break;
    default: // Settings
      for (int i = 0; i < 3; i++) {
        surface->drawFastHLine(x + 1, y + 3 + i * 5, 15, col);
        surface->fillCircle(x + (i == 1 ? 11 : 5), y + 3 + i * 5, 2, col);
      }
  }
}

static void draw_orbit(int cx, int cy, int r, uint16_t bg, uint16_t col) {
  surface->drawArc(cx, cy, r, r - 2, 30, 300, col, bg, true);
  surface->drawArc(cx, cy, r - 7, r - 9, 0, 280, COL_GLOW, bg, true);
  surface->fillCircle(cx, cy, 4, col);
  surface->fillCircle(cx + r - 2, cy + r / 3, 3, col);
}

static void draw_accent_bar(int x, int y, int h, uint16_t col) {
  surface->fillRoundRect(x, y + 6, 3, h - 12, 1, col);
}

static void draw_fit(const char *s, int x, int y, int maxw, uint8_t font, uint16_t fg, uint16_t bg) {
  use_bitmap(font);
  if (maxw < surface->textWidth("..", font)) return;
  surface->setTextColor(fg, bg);
  // Built-in fonts have no Unicode glyphs. Keep one fallback per codepoint.
  char buf[160];
  size_t n = 0;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s ? s : ""); *p && n < sizeof(buf) - 1; p++) {
    if ((*p & 0xC0) == 0x80) continue;
    buf[n++] = *p >= 127 ? '?' : (*p < 32 ? ' ' : *p);
  }
  buf[n] = '\0';
  if (surface->textWidth(buf, font) > maxw) {
    while (n > 0 && surface->textWidth(buf, font) + surface->textWidth("..", font) > maxw) buf[--n] = '\0';
    if (n + 2 < sizeof(buf)) strcat(buf, "..");
  }
  surface->drawString(buf, x, y, font);
}

static void draw_spark(int x, int y, int w, int h, const float *hist, uint16_t col) {
  surface->drawFastHLine(x, y + h - 1, w, COL_LINE);
  if (hist_n < 2) return;
  int prevx = x;
  int prevy = y + h - 1;
  for (uint8_t i = 0; i < hist_n; i++) {
    uint8_t idx = hist_n == HIST_N ? static_cast<uint8_t>((hist_i + i) % HIST_N) : i;
    float v = constrain(hist[idx], 0.0f, 100.0f);
    int px = x + static_cast<int>((w - 1) * i / (float)(hist_n - 1));
    int py = y + h - 1 - static_cast<int>((h - 1) * v / 100.0f);
    if (i > 0) {
      surface->drawLine(prevx, prevy, px, py, col);
      if (py + 1 < y + h && prevy + 1 < y + h) surface->drawLine(prevx, prevy + 1, px, py + 1, col);
    }
    prevx = px;
    prevy = py;
  }
  surface->fillCircle(prevx, prevy, 2, col);
}

static void draw_gauge(int cx, int cy, int r, float pct, const char *label) {
  uint16_t col = bar_color(pct);
  surface->drawArc(cx, cy, r, r - 5, 45, 315, COL_BAR_BG, COL_PANEL, true);
  int span = static_cast<int>(270.0f * constrain(pct, 0.0f, 100.0f) / 100.0f);
  if (span > 1) surface->drawArc(cx, cy, r, r - 5, 45, 45 + span, col, COL_PANEL, true);
  char n[8];
  snprintf(n, sizeof(n), "%.0f", pct);
  use_bitmap(4);
  surface->setTextColor(COL_TEXT, COL_PANEL);
  surface->drawCentreString(n, cx, cy - 15, 4);
  use_bitmap(1);
  surface->setTextColor(COL_MUTED, COL_PANEL);
  surface->drawCentreString("%", cx, cy + 13, 1);
  surface->drawCentreString(label, cx, cy + r - 1, 1);
}

static void draw_badge(int cx, int cy, int count, uint16_t col) {
  surface->fillCircle(cx, cy, 8, col);
  char b[4];
  if (count > 9) {
    snprintf(b, sizeof(b), "9+");
  } else {
    snprintf(b, sizeof(b), "%d", count);
  }
  use_bitmap(1);
  surface->setTextColor(COL_INK, col);
  surface->drawCentreString(b, cx, cy - 4, 1);
}

static void draw_header(const char *title, bool online, int8_t rssi, int alerts, int mesh) {
  surface->fillRect(0, 0, SCREEN_W, 32, COL_BG);
  draw_fit(title, 10, 8, 82, 2, COL_TEXT, COL_BG);
  char clk[8];
  clock_text(clk, sizeof(clk));
  surface->setTextColor(COL_MUTED, COL_BG);
  surface->drawString(clk, 103, 8, 2);
  if (alerts > 0) draw_badge(184, 16, alerts, COL_BAD);
  else draw_icon(3, 176, 7, COL_MUTED);
  if (mesh > 0) draw_badge(210, 16, mesh, COL_ACCENT);
  else draw_icon(4, 202, 7, COL_MUTED);
  draw_icon(5, 232, 7, COL_MUTED);
  use_bitmap(1);
  surface->setTextColor(COL_MUTED, COL_BG);
  surface->drawString("CFG", 253, 12, 1);
  draw_wifi(290, 10, rssi, online);
}

static void draw_header(const char *title, bool online, int8_t rssi) {
  draw_header(title, online, rssi, 0, 0);
}

char ui_header_hit(int16_t x, int16_t y) {
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= 32) return 0;
  if (x >= 168 && x < 198) return 'a';
  if (x >= 198 && x < 226) return 'm';
  if (x >= 228 && x < 280) return 'c';
  return 0;
}

static constexpr int NAV_Y = 204;
static constexpr int NAV_W = SCREEN_W / UI_PAGE_COUNT;

static void draw_footer(uint8_t page) {
  surface->fillRect(0, NAV_Y, SCREEN_W, SCREEN_H - NAV_Y, COL_BG);
  for (int i = 0; i < UI_PAGE_COUNT; i++) {
    int x = i * NAV_W;
    bool selected = i == page;
    uint16_t bg = selected ? COL_PANEL : COL_BG;
    uint16_t fg = selected ? COL_ACCENT : COL_MUTED;
    if (selected) {
      surface->fillRoundRect(x + 3, NAV_Y + 1, NAV_W - 6, 34, 7, bg);
      surface->fillRoundRect(x + 24, NAV_Y + 1, 16, 2, 1, COL_ACCENT);
    }
    draw_icon(i, x + 24, NAV_Y + 5, fg);
    use_bitmap(1);
    surface->setTextColor(fg, bg);
    surface->drawCentreString(page_title(i), x + NAV_W / 2, NAV_Y + 25, 1);
  }
}

char ui_page_hit(int16_t x, int16_t y) {
  if (x < 0 || x >= SCREEN_W || y < NAV_Y || y >= SCREEN_H) return 0;
  return static_cast<char>('0' + x / NAV_W);
}

void ui_begin() {
  band.setColorDepth(16);
  band.createSprite(SCREEN_W, BAND_H); // Direct drawing remains available if allocation fails.
  ui_set_dark(g_dark);
  surface->fillScreen(COL_BG);
}

void ui_splash(const char *line1, const char *line2) {
  surface->fillScreen(COL_BG);
  draw_orbit(160, 62, 28, COL_BG, COL_ACCENT);
  use_display();
  surface->setTextDatum(TC_DATUM);
  surface->setTextColor(COL_TEXT);
  surface->drawString("AuraGo", 160, 105);
  draw_fit(line1, 24, 163, 272, 2, COL_TEXT, COL_BG);
  draw_fit(line2, 24, 188, 272, 2, COL_MUTED, COL_BG);
  surface->fillRoundRect(24, 224, 272, 3, 1, COL_LINE);
  surface->fillRoundRect(24, 224, 64, 3, 1, COL_ACCENT);
}

void ui_pairing(const char *ssid, const char *qr_text, const char *portal_ip) {
  surface->fillScreen(COL_BG);
  draw_header("PAIR", false, 0);

  const char *payload = (qr_text && qr_text[0]) ? qr_text : "";
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  bool ok = payload[0] != '\0' && qrcode_initText(&qrcode, qrcodeData, 3, ECC_MEDIUM, payload) == 0;

  const int quiet = 4;
  const int scale = 4;
  int box_x = 10;
  int box_y = 46;
  int qr_px = 0;
  if (ok) {
    qr_px = (qrcode.size + quiet * 2) * scale;
    surface->fillRoundRect(box_x, box_y, qr_px, qr_px, 4, 0xFFFF);
    int ox = box_x + quiet * scale;
    int oy = box_y + quiet * scale;
    for (uint8_t y = 0; y < qrcode.size; y++) {
      for (uint8_t x = 0; x < qrcode.size; x++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          surface->fillRect(ox + x * scale, oy + y * scale, scale, scale, TFT_BLACK);
        }
      }
    }
  }

  int tx = ok ? box_x + qr_px + 12 : 16;
  int width = SCREEN_W - tx - 8;
  draw_fit("01 / JOIN WI-FI", tx, 51, width, 1, COL_ACCENT, COL_BG);
  draw_fit(ok ? "Scan to join" : "Join manually", tx, 69, width, 2, COL_TEXT, COL_BG);
  draw_fit(ssid && ssid[0] ? ssid : "agocyd-XXXX", tx, 91, width, 2, COL_TEXT, COL_BG);
  draw_fit("No password", tx, 114, width, 2, COL_MUTED, COL_BG);
  draw_fit("02 / CONNECT", tx, 148, width, 1, COL_ACCENT, COL_BG);
  draw_fit("Open in browser", tx, 164, width, 1, COL_MUTED, COL_BG);
  draw_fit((portal_ip && portal_ip[0]) ? portal_ip : "192.168.4.1", tx, 181, width, 2, COL_TEXT, COL_BG);
  draw_fit("Your AuraGo, a glance away.", 12, 218, 296, 2, COL_MUTED, COL_BG);
}

static const char kTokenKeys[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const int kKeyY = 100;
static const int kKeyW = 40;
static const int kKeyH = 26;
static const int kActionY = 206;

void ui_token_entry(const char *prefix, const char *body) {
  surface->fillScreen(COL_BG);
  draw_header("PAIR", false, 0);
  use_bitmap(2);
  surface->setTextColor(COL_MUTED, COL_BG);
  surface->drawString("type 9 characters", 12, 34, 2);
  surface->setTextColor(COL_ACCENT, COL_BG);
  surface->drawString(prefix && prefix[0] ? prefix : "aura_", 12, 62, 2);

  const char *code = body ? body : "";
  size_t n = strlen(code);
  for (int g = 0; g < 3; g++) {
    int x = 90 + g * 74;
    uint16_t bg = COL_PANEL;
    if (n / 3 == static_cast<size_t>(g) && n < 9) {
      bg = COL_LINE;
    }
    surface->fillRoundRect(x, 52, 68, 36, 4, bg);
    char grp[4] = {' ', ' ', ' ', 0};
    for (int i = 0; i < 3; i++) {
      size_t idx = static_cast<size_t>(g * 3 + i);
      if (idx < n) {
        grp[i] = code[idx];
      }
    }
    surface->setTextColor(COL_TEXT, bg);
    surface->drawCentreString(grp, x + 34, 58, 4);
  }

  for (int i = 0; i < 32; i++) {
    int col = i % 8;
    int row = i / 8;
    int x = col * kKeyW;
    int y = kKeyY + row * kKeyH;
    surface->fillRoundRect(x + 2, y + 1, kKeyW - 4, kKeyH - 2, 4, COL_PANEL);
    char lab[2] = {kTokenKeys[i], 0};
    surface->setTextColor(COL_TEXT, COL_PANEL);
    surface->drawCentreString(lab, x + kKeyW / 2, y + 5, 2);
  }

  surface->fillRoundRect(8, kActionY, 140, 28, 4, COL_PANEL);
  surface->setTextColor(COL_MUTED, COL_PANEL);
  surface->drawCentreString("DEL", 78, kActionY + 6, 2);

  uint16_t okBg = n == 9 ? COL_ACCENT : COL_PANEL;
  uint16_t okFg = n == 9 ? COL_INK : COL_MUTED;
  surface->fillRoundRect(172, kActionY, 140, 28, 4, okBg);
  surface->setTextColor(okFg, okBg);
  surface->drawCentreString("OK", 242, kActionY + 6, 2);
}

char ui_token_key_at(int16_t x, int16_t y) {
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return 0;
  if (y >= kActionY) {
    return x < 160 ? '\b' : '\n';
  }
  if (y < kKeyY) {
    return 0;
  }
  int row = (y - kKeyY) / kKeyH;
  int col = x / kKeyW;
  if (row < 0 || row > 3 || col < 0 || col > 7) {
    return 0;
  }
  return kTokenKeys[row * 8 + col];
}

static void draw_btn(int x, int y, int w, int h, const char *label, uint16_t bg, uint16_t fg) {
  surface->fillRoundRect(x, y, w, h, 7, bg);
  use_bitmap(2);
  int text_x = x + (w - surface->textWidth(label, 2)) / 2;
  draw_fit(label, text_x > x + 8 ? text_x : x + 8, y + (h - 16) / 2, w - 16, 2, fg, bg);
}

static void format_active_url(char *out, size_t cap, const DeviceConfig *cfg) {
  if (cfg == nullptr || config_is_demo(cfg)) {
    snprintf(out, cap, "demo");
    return;
  }
  const char *host = cfg->host[0] ? cfg->host : "?";
  snprintf(out, cap, "%s://%s:%u", cfg->use_tls ? "https" : "http", host, cfg->port);
}

void ui_offline(const DeviceConfig *cfg, const NetStatus *st, uint32_t last_ok_ms) {
  ui_set_dark(cfg && cfg->dark_mode);
  surface->fillScreen(COL_BG);
  draw_header("Connect", false, 0);
  draw_card(16, 40, 288, 112);
  draw_accent_bar(16, 40, 112, COL_BAD);
  draw_icon(4, 30, 53, COL_BAD);
  draw_fit("Waiting for link", 57, 50, 234, 4, COL_TEXT, COL_PANEL);
  char url[80];
  format_active_url(url, sizeof(url), cfg);
  draw_fit(url, 30, 86, 260, 2, COL_TEXT, COL_PANEL);
  const char *err = (st && st->error[0]) ? st->error : "waiting for host";
  draw_fit(err, 30, 108, 260, 2, COL_MUTED, COL_PANEL);
  char age[40];
  if (last_ok_ms == 0) {
    snprintf(age, sizeof(age), "no snapshot yet");
  } else {
    snprintf(age, sizeof(age), "last ok %lus ago", (millis() - last_ok_ms) / 1000);
  }
  draw_fit(age, 30, 132, 260, 1, COL_MUTED, COL_PANEL);
  draw_btn(40, 168, 240, 36, "Edit connection", COL_ACCENT, COL_INK);
  surface->setTextColor(COL_MUTED, COL_BG);
  surface->drawCentreString("or hold BOOT 5s", SCREEN_W / 2, 214, 1);
}

char ui_offline_hit(int16_t x, int16_t y) {
  if (y >= 168 && y <= 210 && x >= 40 && x <= 280) {
    return 'e';
  }
  return 0;
}

void ui_settings(const DeviceConfig *cfg, const char *status_line) {
  ui_set_dark(cfg && cfg->dark_mode);
  surface->fillScreen(COL_BG);
  draw_header("Settings", false, 0);
  char url[80];
  format_active_url(url, sizeof(url), cfg);
  use_bitmap(2);
  draw_fit(url, 16, 36, 288, 2, COL_MUTED, COL_BG);

  bool tls = cfg && cfg->use_tls;
  bool dark = cfg && cfg->dark_mode;
  draw_btn(16, 62, 140, 32, tls ? "HTTPS ON" : "HTTPS OFF", tls ? COL_GOOD : COL_PANEL, tls ? COL_INK : COL_TEXT);
  draw_btn(164, 62, 140, 32, dark ? "Dark ON" : "Dark OFF", dark ? COL_GOOD : COL_PANEL, dark ? COL_INK : COL_TEXT);

  surface->setTextColor(COL_MUTED, COL_BG);
  surface->drawString("Port", 8, 108, 2);
  draw_btn(50, 100, 34, 32, "-", COL_PANEL, COL_TEXT);
  char port[8];
  snprintf(port, sizeof(port), "%u", cfg ? cfg->port : 0);
  surface->setTextColor(COL_TEXT, COL_BG);
  surface->drawCentreString(port, 108, 108, 2);
  draw_btn(130, 100, 34, 32, "+", COL_PANEL, COL_TEXT);

  surface->setTextColor(COL_MUTED, COL_BG);
  surface->drawString("Vol", 172, 108, 2);
  draw_btn(212, 100, 34, 32, "-", COL_PANEL, COL_TEXT);
  uint8_t vol = cfg ? cfg->volume : 0;
  char vol_s[4];
  if (vol == 0) {
    snprintf(vol_s, sizeof(vol_s), "off");
  } else {
    snprintf(vol_s, sizeof(vol_s), "%u", vol);
  }
  surface->setTextColor(vol == 0 ? COL_MUTED : COL_TEXT, COL_BG);
  surface->drawCentreString(vol_s, 264, 108, 2);
  draw_btn(282, 100, 34, 32, "+", COL_PANEL, COL_TEXT);

  const char *st = (status_line && status_line[0]) ? status_line : "Test";
  draw_btn(16, 140, 288, 32, st, COL_PANEL, COL_TEXT);
  draw_btn(16, 176, 140, 28, "Save", COL_ACCENT, COL_INK);
  draw_btn(164, 176, 140, 28, "Back", COL_PANEL, COL_TEXT);
  draw_btn(16, 208, 140, 26, "Token", COL_PANEL, COL_TEXT);
  draw_btn(164, 208, 140, 26, "Edit URL", COL_PANEL, COL_TEXT);
}

char ui_settings_hit(int16_t x, int16_t y) {
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return 0;
  if (y >= 62 && y <= 94) {
    return x < 160 ? 'h' : 'd';
  }
  if (y >= 100 && y <= 132) {
    if (x >= 50 && x < 84) {
      return '-';
    }
    if (x >= 130 && x < 164) {
      return '+';
    }
    if (x >= 212 && x < 246) {
      return 'q';
    }
    if (x >= 246 && x < 282) {
      return 'm';
    }
    if (x >= 282) {
      return 'u';
    }
  }
  if (y >= 140 && y <= 172) {
    return 't';
  }
  if (y >= 176 && y <= 204) {
    return x < 160 ? 's' : 'b';
  }
  if (y >= 208) {
    return x < 160 ? 'k' : 'p';
  }
  return 0;
}

static void draw_home_page(const Snapshot *snap, int8_t rssi, bool online) {
  bool busy = snap->agent.busy;
  uint16_t state_col = busy ? COL_WARN : COL_ACCENT;
  surface->fillRoundRect(8, 36, 304, 91, 12, COL_HERO);
  // Contour backdrop, drawn directly in RGB565. No full-screen image in RAM.
  for (int r = 27; r <= 45; r += 9) {
    surface->drawArc(266, 81, r, r - 1, 0, 360, COL_GLOW, COL_HERO, true);
  }
  draw_orbit(266, 81, 24, COL_HERO, state_col);
  use_bitmap(1);
  surface->setTextColor(COL_MUTED, COL_HERO);
  surface->drawString("AURAGO / AGENT", 20, 46, 1);
  use_display();
  surface->setTextColor(COL_TEXT);
  surface->drawString(busy ? "Working" : "Ready", 18, 64);
  draw_fit(snap->agent.model[0] ? snap->agent.model : "Awaiting model", 20, 103, 202, 2, COL_ACCENT, COL_HERO);

  draw_fit(snap->agent.task[0] ? snap->agent.task : "Standing by for your next task", 12, 132, 296, 2, COL_TEXT, COL_BG);
  char last[20], line[64];
  format_last(last, sizeof(last), snap->work.last_user_h);
  snprintf(line, sizeof(line), "%d running / %d queued   Last: %s", snap->work.missions_running, snap->work.missions_queued, last);
  draw_fit(line, 12, 153, 296, 1, COL_MUTED, COL_BG);

  char cpu[20], link[40], up[24];
  snprintf(cpu, sizeof(cpu), "CPU %.0f%%", snap->host.cpu_pct);
  draw_fit(cpu, 12, 181, 68, 1, COL_ACCENT, COL_BG);
  draw_spark(84, 176, 91, 20, hist_cpu, COL_ACCENT);
  format_uptime(up, sizeof(up), snap->host.host_uptime_s);
  snprintf(link, sizeof(link), "%s", g_ip[0] ? g_ip : up);
  draw_fit(link, 194, 174, 114, 1, COL_MUTED, COL_BG);
  snprintf(link, sizeof(link), "%s %ddBm", online ? "LINK" : "OFF", static_cast<int>(rssi));
  draw_fit(link, 194, 188, 114, 1, online ? COL_ACCENT : COL_BAD, COL_BG);
}

static void draw_load_page(const Snapshot *snap) {
  draw_card(8, 36, 96, 101);
  draw_card(112, 36, 96, 101);
  draw_card(216, 36, 96, 101);
  draw_gauge(56, 83, 35, snap->host.cpu_pct, "CPU");
  draw_gauge(160, 83, 35, snap->host.mem_pct, "RAM");
  draw_gauge(264, 83, 35, snap->host.disk_pct, "DISK");
  draw_fit("HISTORY", 12, 147, 90, 1, COL_MUTED, COL_BG);
  draw_fit("CPU", 240, 147, 28, 1, COL_ACCENT, COL_BG);
  draw_fit("RAM", 281, 147, 28, 1, COL_WARN, COL_BG);
  draw_spark(12, 166, 295, 30, hist_cpu, COL_ACCENT);
  draw_spark(12, 166, 295, 30, hist_mem, COL_WARN);
}

static void draw_stat_card(int x, int y, const char *label, const char *value, uint16_t accent, uint8_t icon) {
  draw_card(x, y, 148, 69);
  draw_icon(icon, x + 117, y + 12, accent);
  draw_fit(label, x + 12, y + 11, 104, 1, COL_MUTED, COL_PANEL);
  use_display();
  if (surface->textWidth(value) <= 124) {
    surface->setTextColor(COL_TEXT);
    surface->drawString(value, x + 12, y + 28);
  } else {
    draw_fit(value, x + 12, y + 31, 124, 4, COL_TEXT, COL_PANEL);
  }
}

static void draw_work_page(const Snapshot *snap) {
  char run[16], queued[16], notes[16], last[24];
  snprintf(run, sizeof(run), "%d", snap->work.missions_running);
  snprintf(queued, sizeof(queued), "%d", snap->work.missions_queued);
  snprintf(notes, sizeof(notes), "%d", snap->work.notes_open);
  format_last(last, sizeof(last), snap->work.last_user_h);
  draw_stat_card(8, 36, "Running", run, snap->work.missions_running > 0 ? COL_WARN : COL_GOOD, 2);
  draw_stat_card(164, 36, "Queued", queued, COL_ACCENT, 2);
  draw_stat_card(8, 113, "Open notes", notes, COL_ACCENT, 3);
  draw_stat_card(164, 113, "Last activity", last, COL_ACCENT, 0);
  draw_fit(snap->agent.task[0] ? snap->agent.task : "No active task", 12, 189, 296, 1, COL_MUTED, COL_BG);
}

static uint16_t sev_color(const char *sev) {
  if (sev && (strcasecmp(sev, "critical") == 0 || strcasecmp(sev, "error") == 0)) {
    return COL_BAD;
  }
  if (sev && strcasecmp(sev, "warning") == 0) {
    return COL_WARN;
  }
  return COL_ACCENT;
}

static void draw_row(int y, const char *title, const char *sub, uint16_t accent, uint8_t icon) {
  draw_card(8, y, 304, 43);
  draw_icon(icon, 19, y + 12, accent);
  draw_fit(title && title[0] ? title : "Untitled", 47, y + 5, 252, 2, COL_TEXT, COL_PANEL);
  draw_fit(sub && sub[0] ? sub : " ", 47, y + 27, 252, 1, COL_MUTED, COL_PANEL);
}

static void draw_empty(uint8_t icon, const char *title, const char *body) {
  surface->fillCircle(160, 100, 28, COL_HERO);
  draw_icon(icon, 152, 92, COL_ACCENT);
  use_bitmap(4);
  surface->setTextColor(COL_TEXT, COL_BG);
  surface->drawCentreString(title, 160, 137, 4);
  use_bitmap(2);
  surface->setTextColor(COL_MUTED, COL_BG);
  surface->drawCentreString(body, 160, 173, 2);
}

static void draw_alerts_page(const Snapshot *snap) {
  char head[32];
  snprintf(head, sizeof(head), "%d system alerts", snap->alerts.count);
  draw_fit(head, 12, 40, 296, 2, COL_MUTED, COL_BG);
  if (snap->alerts.n == 0) {
    draw_empty(3, "All clear", "No system alerts to show");
    return;
  }
  for (uint8_t i = 0; i < snap->alerts.n && i < PROTO_FEED_MAX; i++) {
    const FeedItem *it = &snap->alerts.items[i];
    draw_row(61 + i * 47, it->title, it->sev, sev_color(it->sev), 3);
  }
}

static void draw_mesh_page(const Snapshot *snap) {
  char head[32];
  snprintf(head, sizeof(head), "%d unread / MeshCore", snap->mesh.unread);
  draw_fit(head, 12, 40, 296, 2, COL_MUTED, COL_BG);
  if (snap->mesh.n == 0) {
    draw_empty(4, "Inbox clear", "New mesh messages appear here");
    return;
  }
  for (uint8_t i = 0; i < snap->mesh.n && i < PROTO_FEED_MAX; i++) {
    const FeedItem *it = &snap->mesh.items[i];
    const char *body = it->locked ? "Locked message" : (it->body[0] ? it->body : "Message");
    char sub[56];
    if (it->age_s >= 3600) snprintf(sub, sizeof(sub), "%uh / %s", it->age_s / 3600, body);
    else if (it->age_s >= 60) snprintf(sub, sizeof(sub), "%um / %s", it->age_s / 60, body);
    else snprintf(sub, sizeof(sub), "%s", body);
    draw_row(61 + i * 47, it->title, sub, it->locked ? COL_WARN : COL_ACCENT, 4);
  }
}

void ui_render(const Snapshot *snap, uint8_t page, bool online, int8_t rssi, bool dark,
               const NotifyInfo *notification, uint32_t remain_ms) {
  ui_set_dark(dark);
  paint_frame([&]() {
    // fillScreen() uses the base TFT dimensions, not the sprite viewport.
    surface->fillRect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    if (!snap) {
      draw_header("AuraGo", online, rssi);
      draw_empty(4, "Connecting", "Waiting for the first snapshot");
      return;
    }
    if (page >= UI_PAGE_COUNT) page = 0;
    draw_header(page_title(page), online, rssi, snap->alerts.count, snap->mesh.unread);
    if (page == 1) {
      draw_load_page(snap);
    } else if (page == 2) {
      draw_work_page(snap);
    } else if (page == 3) {
      draw_alerts_page(snap);
    } else if (page == 4) {
      draw_mesh_page(snap);
    } else {
      draw_home_page(snap, rssi, online);
    }
    draw_footer(page);
    ui_overlay(notification, remain_ms);
  });
}

void ui_overlay(const NotifyInfo *n, uint32_t remain_ms) {
  if (n == nullptr || !n->active) {
    return;
  }
  uint16_t border = COL_ACCENT;
  int rank = notify_rank(n->priority);
  if (rank >= 3) {
    border = COL_BAD;
  } else if (rank >= 2) {
    border = COL_WARN;
  }
  surface->fillRect(0, 32, SCREEN_W, SCREEN_H - 32, COL_BG);
  surface->fillRoundRect(8, 36, 304, 196, 12, COL_PANEL);
  surface->drawRoundRect(8, 36, 304, 196, 12, border);
  draw_icon(3, 22, 43, border);
  const char *prio = n->priority[0] ? n->priority : "notice";
  draw_fit(prio, 48, 44, 172, 2, border, COL_PANEL);

  char ttl[12];
  snprintf(ttl, sizeof(ttl), "%lu s", remain_ms / 1000);
  surface->setTextColor(COL_MUTED, COL_PANEL);
  surface->drawRightString(ttl, 297, 44, 2);
  const char *title = n->title[0] ? n->title : "AuraGo";
  draw_fit(title, 22, 74, 276, 4, COL_TEXT, COL_PANEL);

  // Wrap within the notification, reserving the dismiss button even at max length.
  use_bitmap(2);
  const char *body = n->body;
  for (int row = 0; row < 4 && *body; row++) {
    char line[PROTO_BODY_MAX + 1];
    size_t len = 0, space = 0;
    while (body[len] && body[len] != '\n' && len < sizeof(line) - 1) {
      line[len] = body[len];
      line[len + 1] = '\0';
      if (surface->textWidth(line, 2) > 276) break;
      if (body[len] == ' ') space = len;
      len++;
    }
    if (body[len] && body[len] != '\n' && space) len = space;
    if (!len && *body != '\n') len = 1;
    if (row == 3) {
      draw_fit(body, 22, 110 + row * 19, 276, 2, COL_MUTED, COL_PANEL);
      break;
    }
    memcpy(line, body, len);
    line[len] = '\0';
    draw_fit(line, 22, 110 + row * 19, 276, 2, COL_MUTED, COL_PANEL);
    body += len;
    while (*body == ' ' || *body == '\n') body++;
  }
  draw_btn(20, 194, 280, 28, "Tap to dismiss", border, COL_INK);
}
