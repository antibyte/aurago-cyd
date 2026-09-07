#include "ui.h"
#include "hardware.h"

#include <Arduino.h>
#include <qrcode.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static uint16_t COL_BG = 0x0000;
static uint16_t COL_PANEL = 0x0A2A;
static uint16_t COL_LINE = 0x1C71;
static uint16_t COL_TEXT = 0xFFFF;
static uint16_t COL_MUTED = 0x8410;
static uint16_t COL_ACCENT = 0x07FF;
static uint16_t COL_GOOD = 0x07E0;
static uint16_t COL_WARN = 0xFE60;
static uint16_t COL_BAD = 0xF800;
static uint16_t COL_BAR_BG = 0x1082;
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
    COL_BG = 0x0000;
    COL_PANEL = 0x0A2A;
    COL_LINE = 0x1C71;
    COL_TEXT = 0xFFFF;
    COL_MUTED = 0x8410;
    COL_ACCENT = 0x07FF;
    COL_GOOD = 0x07E0;
    COL_WARN = 0xFE60;
    COL_BAD = 0xF800;
    COL_BAR_BG = 0x1082;
  } else {
    COL_BG = 0xEF7D;
    COL_PANEL = 0xFFFF;
    COL_LINE = 0xC618;
    COL_TEXT = 0x0000;
    COL_MUTED = 0x6B6D;
    COL_ACCENT = 0x03DF;
    COL_GOOD = 0x03A0;
    COL_WARN = 0xC400;
    COL_BAD = 0xC800;
    COL_BAR_BG = 0xDEFB;
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
  tft.setFreeFont(nullptr);
  tft.setTextFont(font);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
}

static void use_orbitron(bool large) {
  tft.setFreeFont(large ? &Orbitron_Light_32 : &Orbitron_Light_24);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
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
    tft.fillRect(bx, by, 3, h, c);
  }
}

static void draw_card(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 6, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 6, COL_LINE);
}

static void draw_accent_bar(int x, int y, int h, uint16_t col) {
  tft.fillRoundRect(x, y + 6, 3, h - 12, 1, col);
}

static void draw_fit(const char *s, int x, int y, int maxw, uint8_t font, uint16_t fg, uint16_t bg) {
  if (s == nullptr) {
    s = "";
  }
  tft.setTextColor(fg, bg);
  if (tft.textWidth(s, font) <= maxw) {
    tft.drawString(s, x, y, font);
    return;
  }
  char buf[48];
  size_t n = strlen(s);
  if (n >= sizeof(buf)) {
    n = sizeof(buf) - 1;
  }
  memcpy(buf, s, n);
  buf[n] = '\0';
  while (n > 1 && tft.textWidth(buf, font) > maxw) {
    n--;
    buf[n] = '\0';
    if (n >= 2) {
      buf[n - 2] = '.';
      buf[n - 1] = '.';
    }
  }
  tft.drawString(buf, x, y, font);
}

static void draw_spark(int x, int y, int w, int h, const float *hist, uint16_t col) {
  if (hist_n < 2) {
    return;
  }
  int prevx = x;
  int prevy = y + h - 1;
  for (uint8_t i = 0; i < hist_n; i++) {
    uint8_t idx = hist_n == HIST_N ? static_cast<uint8_t>((hist_i + i) % HIST_N) : i;
    float v = constrain(hist[idx], 0.0f, 100.0f);
    int px = x + static_cast<int>((w - 1) * i / (float)(hist_n - 1));
    int py = y + h - 1 - static_cast<int>((h - 1) * v / 100.0f);
    if (i > 0) {
      tft.drawLine(prevx, prevy, px, py, col);
    }
    prevx = px;
    prevy = py;
  }
}

static void draw_gauge(int cx, int cy, int r, float pct, const char *label) {
  uint16_t col = bar_color(pct);
  tft.drawArc(cx, cy, r, r - 7, 45, 315, COL_BAR_BG, COL_BG, true);
  int span = static_cast<int>(270.0f * constrain(pct, 0.0f, 100.0f) / 100.0f);
  if (span > 1) {
    tft.drawArc(cx, cy, r, r - 7, 45, 45 + span, col, COL_BG, true);
  }
  char n[8];
  snprintf(n, sizeof(n), "%.0f", pct);
  use_bitmap(4);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawCentreString(n, cx, cy - 14, 4);
  use_bitmap(2);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawCentreString(label, cx, cy + r - 2, 2);
}

static void draw_badge(int cx, int cy, int count, uint16_t col) {
  if (count <= 0) {
    tft.drawCircle(cx, cy, 8, COL_LINE);
    return;
  }
  tft.fillCircle(cx, cy, 8, col);
  char b[4];
  if (count > 9) {
    snprintf(b, sizeof(b), "9+");
  } else {
    snprintf(b, sizeof(b), "%d", count);
  }
  use_bitmap(1);
  tft.setTextColor(COL_BG, col);
  tft.drawCentreString(b, cx, cy - 4, 1);
}

static void draw_header(const char *title, bool online, int8_t rssi, int alerts, int mesh) {
  tft.fillRect(0, 0, SCREEN_W, 32, COL_PANEL);
  tft.fillRect(0, 0, SCREEN_W, 2, COL_ACCENT);
  char clk[8];
  clock_text(clk, sizeof(clk));
  use_bitmap(2);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString(clk, 8, 9, 2);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString(title, 62, 9, 2);
  draw_badge(184, 16, alerts, COL_BAD);
  draw_badge(210, 16, mesh, COL_ACCENT);
  tft.fillRoundRect(228, 6, 44, 20, 10, COL_ACCENT);
  use_bitmap(2);
  tft.setTextColor(COL_BG, COL_ACCENT);
  tft.drawCentreString("CFG", 250, 9, 2);
  draw_wifi(284, 10, rssi, online);
  tft.drawFastHLine(0, 32, SCREEN_W, COL_LINE);
}

static void draw_header(const char *title, bool online, int8_t rssi) {
  draw_header(title, online, rssi, 0, 0);
}

char ui_header_hit(int16_t x, int16_t y) {
  if (y > 32) {
    return 0;
  }
  if (x >= 168 && x < 198) {
    return 'a';
  }
  if (x >= 198 && x < 226) {
    return 'm';
  }
  if (x >= 228 && x < 276) {
    return 'c';
  }
  return 0;
}

static void draw_footer(uint8_t page) {
  tft.fillRect(0, 218, SCREEN_W, 22, COL_PANEL);
  tft.drawFastHLine(0, 218, SCREEN_W, COL_LINE);
  const int n = UI_PAGE_COUNT;
  const int gap = 16;
  int start = SCREEN_W / 2 - (n * gap) / 2 + 2;
  for (int i = 0; i < n; i++) {
    int cx = start + i * gap;
    if (i == page) {
      tft.fillRoundRect(cx - 6, 226, 12, 6, 3, COL_ACCENT);
    } else {
      tft.fillCircle(cx, 229, 3, COL_LINE);
    }
  }
  use_bitmap(1);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString("<", 10, 224, 2);
  tft.drawRightString(">", 310, 224, 2);
}

char ui_page_hit(int16_t x, int16_t y) {
  if (y < 218) {
    return 0;
  }
  if (x < 40) {
    return '<';
  }
  if (x > 280) {
    return '>';
  }
  const int n = UI_PAGE_COUNT;
  const int gap = 16;
  int start = SCREEN_W / 2 - (n * gap) / 2 + 2;
  for (int i = 0; i < n; i++) {
    int cx = start + i * gap;
    if (x >= cx - 8 && x <= cx + 8) {
      return static_cast<char>('0' + i);
    }
  }
  return 0;
}

void ui_begin() {
  ui_set_dark(g_dark);
  tft.fillScreen(COL_BG);
}

void ui_splash(const char *line1, const char *line2) {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, SCREEN_W, 3, COL_ACCENT);
  use_orbitron(true);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COL_ACCENT);
  tft.drawString("AURAGO", SCREEN_W / 2, 72);
  use_bitmap(2);
  tft.setTextColor(COL_TEXT, COL_BG);
  if (line1) {
    tft.drawCentreString(line1, SCREEN_W / 2, 128, 2);
  }
  if (line2) {
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawCentreString(line2, SCREEN_W / 2, 150, 2);
  }
}

void ui_pairing(const char *ssid, const char *qr_text, const char *portal_ip) {
  tft.fillScreen(COL_BG);
  draw_header("PAIR", false, 0);

  const char *payload = (qr_text && qr_text[0]) ? qr_text : "";
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  bool ok = payload[0] != '\0' && qrcode_initText(&qrcode, qrcodeData, 3, ECC_MEDIUM, payload) == 0;

  const int quiet = 3;
  const int scale = 5;
  int box_x = 10;
  int box_y = 38;
  int qr_px = 0;
  if (ok) {
    qr_px = (qrcode.size + quiet * 2) * scale;
    tft.fillRoundRect(box_x, box_y, qr_px, qr_px, 4, 0xFFFF);
    int ox = box_x + quiet * scale;
    int oy = box_y + quiet * scale;
    for (uint8_t y = 0; y < qrcode.size; y++) {
      for (uint8_t x = 0; x < qrcode.size; x++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          tft.fillRect(ox + x * scale, oy + y * scale, scale, scale, TFT_BLACK);
        }
      }
    }
  }

  int tx = ok ? box_x + qr_px + 10 : 16;
  use_bitmap(2);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Scan to join", tx, 48, 2);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString(ssid && ssid[0] ? ssid : "agocyd-XXXX", tx, 74, 2);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("open Wi-Fi", tx, 108, 2);
  tft.drawString("no password", tx, 128, 2);
  tft.drawString("then open", tx, 160, 2);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString((portal_ip && portal_ip[0]) ? portal_ip : "192.168.4.1", tx, 182, 2);
}

static const char kTokenKeys[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const int kKeyY = 100;
static const int kKeyW = 40;
static const int kKeyH = 26;
static const int kActionY = 206;

void ui_token_entry(const char *prefix, const char *body) {
  tft.fillScreen(COL_BG);
  draw_header("PAIR", false, 0);
  use_bitmap(2);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("type 9 characters", 12, 34, 2);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString(prefix && prefix[0] ? prefix : "aura_", 12, 62, 2);

  const char *code = body ? body : "";
  size_t n = strlen(code);
  for (int g = 0; g < 3; g++) {
    int x = 90 + g * 74;
    uint16_t bg = COL_PANEL;
    if (n / 3 == static_cast<size_t>(g) && n < 9) {
      bg = COL_LINE;
    }
    tft.fillRoundRect(x, 52, 68, 36, 4, bg);
    char grp[4] = {' ', ' ', ' ', 0};
    for (int i = 0; i < 3; i++) {
      size_t idx = static_cast<size_t>(g * 3 + i);
      if (idx < n) {
        grp[i] = code[idx];
      }
    }
    tft.setTextColor(COL_TEXT, bg);
    tft.drawCentreString(grp, x + 34, 58, 4);
  }

  for (int i = 0; i < 32; i++) {
    int col = i % 8;
    int row = i / 8;
    int x = col * kKeyW;
    int y = kKeyY + row * kKeyH;
    tft.fillRect(x + 1, y + 1, kKeyW - 2, kKeyH - 2, COL_PANEL);
    char lab[2] = {kTokenKeys[i], 0};
    tft.setTextColor(COL_TEXT, COL_PANEL);
    tft.drawCentreString(lab, x + kKeyW / 2, y + 5, 2);
  }

  tft.fillRoundRect(8, kActionY, 140, 28, 4, COL_PANEL);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawCentreString("DEL", 78, kActionY + 6, 2);

  uint16_t okBg = n == 9 ? COL_ACCENT : COL_PANEL;
  uint16_t okFg = n == 9 ? COL_BG : COL_MUTED;
  tft.fillRoundRect(172, kActionY, 140, 28, 4, okBg);
  tft.setTextColor(okFg, okBg);
  tft.drawCentreString("OK", 242, kActionY + 6, 2);
}

char ui_token_key_at(int16_t x, int16_t y) {
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
  tft.fillRoundRect(x, y, w, h, 4, bg);
  tft.setTextColor(fg, bg);
  tft.drawCentreString(label, x + w / 2, y + 8, 2);
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
  tft.fillScreen(COL_BG);
  draw_header("OFFLINE", false, 0);
  draw_card(16, 40, 288, 112);
  draw_accent_bar(16, 40, 112, COL_BAD);
  use_orbitron(false);
  tft.setTextColor(COL_BAD);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("NO LINK", SCREEN_W / 2, 52);
  use_bitmap(2);
  char url[80];
  format_active_url(url, sizeof(url), cfg);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawCentreString(url, SCREEN_W / 2, 86, 2);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  const char *err = (st && st->error[0]) ? st->error : "waiting for host";
  tft.drawCentreString(err, SCREEN_W / 2, 108, 2);
  char age[40];
  if (last_ok_ms == 0) {
    snprintf(age, sizeof(age), "no snapshot yet");
  } else {
    snprintf(age, sizeof(age), "last ok %lus ago", (millis() - last_ok_ms) / 1000);
  }
  tft.drawCentreString(age, SCREEN_W / 2, 128, 2);
  draw_btn(40, 168, 240, 36, "Edit", COL_ACCENT, COL_BG);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawCentreString("or hold BOOT 5s", SCREEN_W / 2, 214, 1);
}

char ui_offline_hit(int16_t x, int16_t y) {
  if (y >= 168 && y <= 210 && x >= 40 && x <= 280) {
    return 'e';
  }
  return 0;
}

void ui_settings(const DeviceConfig *cfg, const char *status_line) {
  ui_set_dark(cfg && cfg->dark_mode);
  tft.fillScreen(COL_BG);
  draw_header("SETUP", false, 0);
  char url[80];
  format_active_url(url, sizeof(url), cfg);
  use_bitmap(2);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawCentreString(url, SCREEN_W / 2, 36, 2);

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("HTTPS", 16, 70, 2);
  bool tls = cfg && cfg->use_tls;
  draw_btn(86, 62, 58, 32, tls ? "ON" : "OFF", tls ? COL_GOOD : COL_PANEL, tls ? COL_BG : COL_TEXT);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Dark", 156, 70, 2);
  bool dark = cfg && cfg->dark_mode;
  draw_btn(214, 62, 90, 32, dark ? "ON" : "OFF", dark ? COL_GOOD : COL_PANEL, dark ? COL_BG : COL_TEXT);

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Port", 16, 108, 2);
  draw_btn(140, 100, 44, 32, "-", COL_PANEL, COL_TEXT);
  char port[8];
  snprintf(port, sizeof(port), "%u", cfg ? cfg->port : 0);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawCentreString(port, 228, 108, 2);
  draw_btn(260, 100, 44, 32, "+", COL_PANEL, COL_TEXT);

  const char *st = (status_line && status_line[0]) ? status_line : "Test";
  draw_btn(16, 140, 288, 32, st, COL_ACCENT, COL_BG);
  draw_btn(16, 176, 140, 28, "Save", COL_GOOD, COL_BG);
  draw_btn(164, 176, 140, 28, "Back", COL_PANEL, COL_TEXT);
  draw_btn(16, 208, 140, 26, "Token", COL_PANEL, COL_TEXT);
  draw_btn(164, 208, 140, 26, "Portal", COL_PANEL, COL_TEXT);
}

char ui_settings_hit(int16_t x, int16_t y) {
  if (y >= 62 && y <= 94) {
    if (x >= 86 && x < 156) {
      return 'h';
    }
    if (x >= 214) {
      return 'd';
    }
  }
  if (y >= 100 && y <= 132) {
    if (x >= 140 && x <= 184) {
      return '-';
    }
    if (x >= 260) {
      return '+';
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
  uint16_t state_col = busy ? COL_WARN : COL_GOOD;
  draw_card(10, 40, 300, 88);
  char clk[8];
  clock_text(clk, sizeof(clk));
  use_orbitron(true);
  tft.setTextColor(COL_TEXT);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(clk, 22, 48);
  use_bitmap(2);
  tft.fillRoundRect(200, 50, 96, 24, 12, state_col);
  tft.setTextColor(COL_BG, state_col);
  tft.drawCentreString(busy ? "Busy" : "Idle", 248, 54, 2);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  const char *model = snap->agent.model[0] ? snap->agent.model : "model";
  tft.drawString(model, 22, 100, 2);
  const char *task = snap->agent.task[0] ? snap->agent.task : "waiting";
  draw_fit(task, 120, 100, 178, 2, COL_TEXT, COL_PANEL);

  draw_card(10, 134, 145, 40);
  draw_card(165, 134, 145, 40);
  use_bitmap(1);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString("LAST", 20, 138, 1);
  tft.drawString("MISSIONS", 175, 138, 1);
  char last[24];
  format_last(last, sizeof(last), snap->work.last_user_h);
  use_bitmap(2);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString(last, 20, 150, 2);
  char miss[24];
  snprintf(miss, sizeof(miss), "%d run  %d q", snap->work.missions_running, snap->work.missions_queued);
  tft.drawString(miss, 175, 150, 2);

  draw_card(10, 178, 300, 36);
  tft.drawFastHLine(20, 196, 280, COL_LINE);
  draw_spark(20, 184, 280, 24, hist_cpu, COL_ACCENT);
  use_bitmap(1);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  char cpu[16];
  snprintf(cpu, sizeof(cpu), "CPU %.0f%%", snap->host.cpu_pct);
  tft.drawString(cpu, 20, 180, 1);
  char up[24];
  format_uptime(up, sizeof(up), snap->host.host_uptime_s);
  char link[40];
  if (g_ip[0]) {
    snprintf(link, sizeof(link), "%s  %ddBm", g_ip, static_cast<int>(rssi));
  } else {
    snprintf(link, sizeof(link), "%s  %ddBm", up, static_cast<int>(rssi));
  }
  tft.setTextColor(online ? COL_ACCENT : COL_BAD, COL_PANEL);
  tft.drawRightString(link, 300, 180, 1);
}

static void draw_load_page(const Snapshot *snap) {
  draw_gauge(56, 96, 40, snap->host.cpu_pct, "CPU");
  draw_gauge(160, 96, 40, snap->host.mem_pct, "RAM");
  draw_gauge(264, 96, 40, snap->host.disk_pct, "DSK");

  draw_card(10, 168, 300, 46);
  tft.drawFastHLine(20, 191, 280, COL_LINE);
  draw_spark(20, 176, 280, 30, hist_cpu, COL_ACCENT);
  draw_spark(20, 176, 280, 30, hist_mem, COL_WARN);
  use_bitmap(1);
  tft.setTextColor(COL_ACCENT, COL_PANEL);
  tft.drawString("CPU", 20, 172, 1);
  tft.setTextColor(COL_WARN, COL_PANEL);
  tft.drawString("RAM", 52, 172, 1);
}

static void draw_stat_card(int x, int y, int w, int h, const char *label, const char *value, uint16_t accent) {
  draw_card(x, y, w, h);
  draw_accent_bar(x, y, h, accent);
  use_bitmap(1);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString(label, x + 12, y + 8, 1);
  use_bitmap(7);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString(value, x + 12, y + 24, 7);
  use_bitmap(2);
}

static void draw_work_page(const Snapshot *snap) {
  char run[8];
  char queued[8];
  char notes[8];
  snprintf(run, sizeof(run), "%d", snap->work.missions_running);
  snprintf(queued, sizeof(queued), "%d", snap->work.missions_queued);
  snprintf(notes, sizeof(notes), "%d", snap->work.notes_open);

  draw_stat_card(10, 36, 145, 80, "RUNNING", run, snap->work.missions_running > 0 ? COL_WARN : COL_GOOD);
  draw_stat_card(165, 36, 145, 80, "QUEUED", queued, COL_ACCENT);

  draw_card(10, 122, 145, 88);
  draw_accent_bar(10, 122, 88, COL_ACCENT);
  use_bitmap(1);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString("NOTES", 22, 130, 1);
  use_bitmap(7);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString(notes, 22, 148, 7);

  draw_card(165, 122, 145, 88);
  draw_accent_bar(165, 122, 88, COL_GOOD);
  use_bitmap(1);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString("LAST USER", 177, 130, 1);
  char last[24];
  format_last(last, sizeof(last), snap->work.last_user_h);
  use_orbitron(false);
  tft.setTextColor(COL_TEXT);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(last, 177, 154);
  use_bitmap(2);
  const char *task = snap->agent.task[0] ? snap->agent.task : "idle";
  draw_fit(task, 177, 182, 120, 2, COL_MUTED, COL_PANEL);
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

static void draw_row(int y, const char *title, const char *sub, uint16_t accent) {
  draw_card(10, y, 300, 42);
  tft.fillCircle(24, y + 21, 5, accent);
  use_bitmap(2);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  draw_fit(title && title[0] ? title : "-", 40, y + 6, 250, 2, COL_TEXT, COL_PANEL);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  draw_fit(sub && sub[0] ? sub : " ", 40, y + 24, 250, 1, COL_MUTED, COL_PANEL);
}

static void draw_alerts_page(const Snapshot *snap) {
  use_bitmap(2);
  tft.setTextColor(COL_MUTED, COL_BG);
  char head[24];
  snprintf(head, sizeof(head), "%d warnings", snap->alerts.count);
  tft.drawString(head, 12, 40, 2);
  if (snap->alerts.n == 0) {
    draw_card(10, 68, 300, 48);
    tft.setTextColor(COL_GOOD, COL_PANEL);
    tft.drawString("All clear", 24, 84, 2);
    return;
  }
  for (uint8_t i = 0; i < snap->alerts.n; i++) {
    const FeedItem *it = &snap->alerts.items[i];
    draw_row(68 + i * 48, it->title, it->sev, sev_color(it->sev));
  }
}

static void draw_mesh_page(const Snapshot *snap) {
  use_bitmap(2);
  tft.setTextColor(COL_MUTED, COL_BG);
  char head[24];
  snprintf(head, sizeof(head), "%d unread", snap->mesh.unread);
  tft.drawString(head, 12, 40, 2);
  if (snap->mesh.n == 0) {
    draw_card(10, 68, 300, 48);
    tft.setTextColor(COL_MUTED, COL_PANEL);
    tft.drawString("No messages", 24, 84, 2);
    return;
  }
  for (uint8_t i = 0; i < snap->mesh.n; i++) {
    const FeedItem *it = &snap->mesh.items[i];
    const char *body = it->locked ? "locked" : (it->body[0] ? it->body : "message");
    char sub[40];
    if (it->age_s >= 3600) {
      snprintf(sub, sizeof(sub), "%uh  %s", it->age_s / 3600, body);
    } else if (it->age_s >= 60) {
      snprintf(sub, sizeof(sub), "%um  %s", it->age_s / 60, body);
    } else {
      snprintf(sub, sizeof(sub), "%s", body);
    }
    draw_row(68 + i * 48, it->title, sub, it->locked ? COL_WARN : COL_ACCENT);
  }
}

void ui_render(const Snapshot *snap, uint8_t page, bool online, int8_t rssi, bool dark) {
  ui_set_dark(dark);
  tft.fillScreen(COL_BG);
  if (page >= UI_PAGE_COUNT) {
    page = 0;
  }
  int alerts = snap ? snap->alerts.count : 0;
  int mesh = snap ? snap->mesh.unread : 0;
  draw_header(page_title(page), online, rssi, alerts, mesh);
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
  tft.fillRoundRect(10, 28, SCREEN_W - 20, SCREEN_H - 44, 8, COL_PANEL);
  tft.drawRoundRect(10, 28, SCREEN_W - 20, SCREEN_H - 44, 8, border);
  use_bitmap(2);
  tft.setTextColor(border, COL_PANEL);
  const char *prio = n->priority[0] ? n->priority : "notice";
  tft.drawString(prio, 22, 40, 2);

  char ttl[12];
  snprintf(ttl, sizeof(ttl), "%lu s", remain_ms / 1000);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawRightString(ttl, 300, 40, 2);

  use_orbitron(false);
  tft.setTextColor(COL_TEXT);
  tft.setTextDatum(TL_DATUM);
  const char *title = n->title[0] ? n->title : "AuraGo";
  tft.drawString(title, 22, 72);

  use_bitmap(2);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.setTextWrap(true, false);
  tft.setCursor(22, 118);
  tft.setTextFont(2);
  tft.print(n->body[0] ? n->body : "");
  tft.setTextWrap(false, false);

  tft.setTextColor(COL_ACCENT, COL_PANEL);
  tft.drawCentreString("tap to dismiss", SCREEN_W / 2, 188, 2);
}
