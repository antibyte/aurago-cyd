#include "ui.h"
#include "hardware.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const uint16_t COL_BG = 0x10A3;
static const uint16_t COL_PANEL = 0x2128;
static const uint16_t COL_LINE = 0x39C8;
static const uint16_t COL_TEXT = 0xEF7D;
static const uint16_t COL_MUTED = 0x8C71;
static const uint16_t COL_ACCENT = 0x3D9C;
static const uint16_t COL_GOOD = 0x07E4;
static const uint16_t COL_WARN = 0xFE60;
static const uint16_t COL_BAD = 0xF800;
static const uint16_t COL_BAR_BG = 0x18C4;

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
    snprintf(out, cap, "%ud %uh", d, h);
  } else if (h > 0) {
    snprintf(out, cap, "%uh %um", h, m);
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
      snprintf(out, cap, "just now");
    } else {
      snprintf(out, cap, "%dm ago", mins);
    }
    return;
  }
  if (hours < 48.0f) {
    snprintf(out, cap, "%.0fh ago", hours);
    return;
  }
  snprintf(out, cap, "%.0fd ago", hours / 24.0f);
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

static void draw_bar(int x, int y, int w, int h, float pct, const char *label) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s  %.0f%%", label, pct);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(buf, x, y, 2);
  int by = y + 16;
  tft.fillRoundRect(x, by, w, h, 3, COL_BAR_BG);
  int fill = static_cast<int>((w - 2) * constrain(pct, 0.0f, 100.0f) / 100.0f);
  if (fill > 0) {
    tft.fillRoundRect(x + 1, by + 1, fill, h - 2, 2, bar_color(pct));
  }
}

static void draw_header(const char *title, bool online, int8_t rssi) {
  tft.fillRect(0, 0, SCREEN_W, 28, COL_PANEL);
  tft.setTextColor(COL_ACCENT, COL_PANEL);
  tft.drawString("AURAGO", 8, 7, 2);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString(title, 78, 7, 2);
  char clk[8];
  clock_text(clk, sizeof(clk));
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawRightString(clk, 286, 7, 2);
  draw_wifi(294, 8, rssi, online);
  tft.drawFastHLine(0, 28, SCREEN_W, COL_LINE);
}

static void draw_footer(uint8_t page) {
  tft.fillRect(0, 224, SCREEN_W, 16, COL_PANEL);
  tft.drawFastHLine(0, 224, SCREEN_W, COL_LINE);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  char buf[8];
  snprintf(buf, sizeof(buf), "%u/2", page + 1);
  tft.drawRightString(buf, 312, 226, 1);
  tft.drawString("swipe", 8, 226, 1);
}

void ui_begin() {
  tft.fillScreen(COL_BG);
}

void ui_splash(const char *line1, const char *line2) {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawCentreString("AURAGO CYD", SCREEN_W / 2, 88, 4);
  tft.setTextColor(COL_TEXT, COL_BG);
  if (line1) {
    tft.drawCentreString(line1, SCREEN_W / 2, 128, 2);
  }
  if (line2) {
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawCentreString(line2, SCREEN_W / 2, 150, 2);
  }
}

void ui_offline(const NetStatus *st, uint32_t last_ok_ms) {
  tft.fillScreen(COL_BG);
  draw_header("offline", false, 0);
  tft.setTextColor(COL_BAD, COL_BG);
  tft.drawCentreString("AuraGo unreachable", SCREEN_W / 2, 80, 2);
  tft.setTextColor(COL_MUTED, COL_BG);
  const char *err = (st && st->error[0]) ? st->error : "waiting for host";
  tft.drawCentreString(err, SCREEN_W / 2, 108, 2);
  char age[40];
  if (last_ok_ms == 0) {
    snprintf(age, sizeof(age), "no snapshot yet");
  } else {
    snprintf(age, sizeof(age), "last ok %lus ago", (millis() - last_ok_ms) / 1000);
  }
  tft.drawCentreString(age, SCREEN_W / 2, 132, 2);
  tft.drawCentreString("hold BOOT 5s to reconfigure", SCREEN_W / 2, 180, 1);
}

static void draw_status_page(const Snapshot *snap) {
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("STATE", 12, 40, 1);
  tft.setTextColor(snap->agent.busy ? COL_WARN : COL_GOOD, COL_BG);
  tft.drawString(snap->agent.busy ? "busy" : "idle", 12, 54, 4);

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(snap->agent.model[0] ? snap->agent.model : "no model", 120, 62, 2);

  tft.drawFastHLine(12, 92, SCREEN_W - 24, COL_LINE);

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("NOW", 12, 102, 1);
  tft.setTextColor(COL_TEXT, COL_BG);
  const char *task = snap->agent.task[0] ? snap->agent.task : "waiting";
  tft.drawString(task, 12, 116, 2);

  char last[24];
  format_last(last, sizeof(last), snap->work.last_user_h);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("LAST", 12, 144, 1);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(last, 12, 158, 2);

  char work[48];
  snprintf(work, sizeof(work), "missions %d run / %d queued",
           snap->work.missions_running, snap->work.missions_queued);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(work, 12, 186, 2);

  char notes[32];
  snprintf(notes, sizeof(notes), "notes %d open", snap->work.notes_open);
  tft.drawString(notes, 12, 204, 2);
}

static void draw_load_page(const Snapshot *snap) {
  draw_bar(12, 40, SCREEN_W - 24, 14, snap->host.cpu_pct, "CPU");
  draw_bar(12, 86, SCREEN_W - 24, 14, snap->host.mem_pct, "RAM");
  draw_bar(12, 132, SCREEN_W - 24, 14, snap->host.disk_pct, "DSK");

  char a[24];
  char h[24];
  format_uptime(a, sizeof(a), snap->host.uptime_s);
  format_uptime(h, sizeof(h), snap->host.host_uptime_s);
  tft.setTextColor(COL_MUTED, COL_BG);
  char line[64];
  snprintf(line, sizeof(line), "agent %s   host %s", a, h);
  tft.drawString(line, 12, 186, 2);

  char notes[32];
  snprintf(notes, sizeof(notes), "notes %d open", snap->work.notes_open);
  tft.drawString(notes, 12, 204, 2);
}

void ui_render(const Snapshot *snap, uint8_t page, bool online, int8_t rssi) {
  tft.fillScreen(COL_BG);
  draw_header(page == 0 ? "status" : "load", online, rssi);
  if (page == 0) {
    draw_status_page(snap);
  } else {
    draw_load_page(snap);
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
  tft.setTextColor(border, COL_PANEL);
  const char *prio = n->priority[0] ? n->priority : "notice";
  char head[24];
  snprintf(head, sizeof(head), "%s", prio);
  tft.drawString(head, 22, 40, 2);

  char ttl[12];
  snprintf(ttl, sizeof(ttl), "%lu s", remain_ms / 1000);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawRightString(ttl, 300, 40, 2);

  tft.setTextColor(COL_TEXT, COL_PANEL);
  const char *title = n->title[0] ? n->title : "AuraGo";
  tft.drawString(title, 22, 72, 4);

  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.setTextWrap(true, false);
  tft.setCursor(22, 118);
  tft.setTextFont(2);
  tft.print(n->body[0] ? n->body : "");
  tft.setTextWrap(false, false);

  tft.setTextColor(COL_ACCENT, COL_PANEL);
  tft.drawCentreString("tap to dismiss", SCREEN_W / 2, 188, 2);
}
