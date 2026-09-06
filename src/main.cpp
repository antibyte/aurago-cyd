#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "config_store.h"
#include "hardware.h"
#include "net_client.h"
#include "protocol.h"
#include "provision.h"
#include "ui.h"

static DeviceConfig cfg;
static Snapshot snap;
static uint8_t page = 0;
static bool overlay_open = false;
static uint32_t overlay_until = 0;
static uint32_t last_poll = 0;
static uint32_t last_heartbeat = 0;
static uint32_t last_draw = 0;
static uint32_t last_brightness = 0;
static uint32_t last_ok = 0;
static bool have_snap = false;
static LedColor forced_led = LedColor::Off;
static bool have_forced_led = false;
static bool settings_open = false;
static DeviceConfig edit_cfg;
static char settings_status[48];

static LedColor led_from_name(const char *name) {
  if (name == nullptr) {
    return LedColor::Green;
  }
  if (strcasecmp(name, "red") == 0) {
    return LedColor::Red;
  }
  if (strcasecmp(name, "yellow") == 0) {
    return LedColor::Yellow;
  }
  if (strcasecmp(name, "blue") == 0) {
    return LedColor::Blue;
  }
  if (strcasecmp(name, "off") == 0) {
    return LedColor::Off;
  }
  return LedColor::Green;
}

static void apply_led() {
  if (overlay_open && notify_rank(snap.notify.priority) >= 3) {
    hardware_set_led(LedColor::Red);
    return;
  }
  NetStatus st = net_status();
  if (!cfg.demo && !st.online) {
    hardware_set_led(st.wifi ? LedColor::Red : LedColor::Blue);
    return;
  }
  if (have_forced_led) {
    hardware_set_led(forced_led);
    return;
  }
  if (snap.display.led[0] != '\0') {
    hardware_set_led(led_from_name(snap.display.led));
    return;
  }
  hardware_set_led(snap.agent.busy ? LedColor::Yellow : LedColor::Green);
}

static void fill_demo(Snapshot *s) {
  snapshot_clear(s);
  uint32_t t = millis();
  s->ts = static_cast<uint32_t>(time(nullptr));
  s->agent.busy = ((t / 9000) % 2) == 1;
  copy_trunc(s->agent.model, sizeof(s->agent.model), "demo");
  copy_trunc(s->agent.personality, sizeof(s->agent.personality), "default");
  if (s->agent.busy) {
    copy_trunc(s->agent.task, sizeof(s->agent.task), "indexing workspace");
  } else {
    s->agent.task[0] = '\0';
  }
  s->host.cpu_pct = 28.0f + static_cast<float>((t / 1000) % 37);
  s->host.mem_pct = 51.0f + static_cast<float>((t / 1500) % 20);
  s->host.disk_pct = 24.0f;
  s->host.uptime_s = t / 1000;
  s->host.host_uptime_s = 86400 + t / 1000;
  s->work.missions_running = s->agent.busy ? 1 : 0;
  s->work.missions_queued = 2;
  s->work.notes_open = 3;
  s->work.last_user_h = 0.2f;
  copy_trunc(s->display.led, sizeof(s->display.led), s->agent.busy ? "yellow" : "green");
}

static bool overlay_replace_ok(const NotifyInfo *incoming) {
  if (!overlay_open || !snap.notify.active) {
    return true;
  }
  return notify_rank(incoming->priority) >= notify_rank(snap.notify.priority);
}

static void open_overlay(const NotifyInfo *n) {
  if (n == nullptr || !n->active) {
    return;
  }
  if (!overlay_replace_ok(n)) {
    return;
  }
  snap.notify = *n;
  snap.notify.active = true;
  overlay_open = true;
  overlay_until = millis() + static_cast<uint32_t>(notify_ttl(n)) * 1000UL;
}

static void close_overlay(bool ack) {
  if (ack && snap.notify.id[0] != '\0') {
    net_send_ack(snap.notify.id);
  }
  snap.notify.active = false;
  snap.notify.id[0] = '\0';
  overlay_open = false;
  overlay_until = 0;
}

static void redraw() {
  NetStatus st = net_status();
  bool online = cfg.demo || st.online;
  if (!have_snap) {
    ui_offline(&cfg, &st, last_ok);
  } else if (!online) {
    ui_offline(&cfg, &st, last_ok);
  } else {
    ui_render(&snap, page, online, hardware_wifi_rssi(), cfg.dark_mode);
    if (overlay_open && snap.notify.active) {
      uint32_t remain = overlay_until > millis() ? overlay_until - millis() : 0;
      ui_overlay(&snap.notify, remain);
    }
  }
  apply_led();
  last_draw = millis();
}

static void apply_ws_events() {
  WsEvent ev;
  bool dirty = false;
  while (net_take_event(&ev)) {
    switch (ev.type) {
      case WsType::Snapshot:
        snap = ev.snapshot;
        have_snap = true;
        last_ok = millis();
        ui_note_metrics(snap.host.cpu_pct, snap.host.mem_pct, snap.host.disk_pct);
        if (snap.display.page[0] && strcasecmp(snap.display.page, "status") != 0 &&
            strcasecmp(snap.display.page, "home") != 0) {
          page = ui_page_from_name(snap.display.page);
        }
        if (snap.notify.active) {
          open_overlay(&snap.notify);
        }
        dirty = true;
        break;
      case WsType::Notify:
        open_overlay(&ev.notify);
        dirty = true;
        break;
      case WsType::Clear:
        if (ev.id[0] == '\0' || strcmp(ev.id, snap.notify.id) == 0) {
          close_overlay(false);
          dirty = true;
        }
        break;
      case WsType::Led:
        forced_led = led_from_name(ev.led);
        have_forced_led = true;
        copy_trunc(snap.display.led, sizeof(snap.display.led), ev.led);
        dirty = true;
        break;
      case WsType::Page:
        page = ui_page_from_name(ev.page);
        dirty = true;
        break;
      default:
        break;
    }
  }
  if (dirty) {
    redraw();
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("aurago-cyd " FIRMWARE_VERSION);

  snapshot_clear(&snap);
  hardware_begin();
  ui_begin();
  ui_splash("starting", FIRMWARE_VERSION);

  config_load(&cfg);
  ui_set_dark(cfg.dark_mode);

  bool force_portal = hardware_boot_held(5000);
  if (force_portal) {
    ui_splash("config reset", "join agocyd-XXXX");
    config_clear();
    config_load(&cfg);
    ui_set_dark(cfg.dark_mode);
  } else {
    ui_splash("connecting Wi-Fi", "hold BOOT 5s to reset");
  }

  bool wifi_ok = provision_connect(&cfg, force_portal);
  if (!wifi_ok) {
    ui_splash("Wi-Fi failed", "reboot or hold BOOT");
  } else {
    provision_enter_token(&cfg);
    char line[40];
    snprintf(line, sizeof(line), "%s", WiFi.localIP().toString().c_str());
    ui_set_link_info(line);
    ui_splash(cfg.demo ? "demo mode" : cfg.host, line);
    delay(800);
  }

  net_begin(&cfg);
  last_poll = 0;
  last_heartbeat = 0;

  if (cfg.demo) {
    fill_demo(&snap);
    have_snap = true;
    last_ok = millis();
    ui_note_metrics(snap.host.cpu_pct, snap.host.mem_pct, snap.host.disk_pct);
  }
  redraw();
}

static void cycle_page(int dir) {
  int n = static_cast<int>(page) + dir;
  if (n < 0) {
    n = UI_PAGE_COUNT - 1;
  }
  if (n >= UI_PAGE_COUNT) {
    n = 0;
  }
  page = static_cast<uint8_t>(n);
}

static void open_settings() {
  edit_cfg = cfg;
  settings_status[0] = '\0';
  settings_open = true;
  ui_settings(&edit_cfg, settings_status);
}

static void settings_test() {
  config_format_url(&edit_cfg);
  net_reconfigure(&edit_cfg);
  snprintf(settings_status, sizeof(settings_status), "testing...");
  ui_settings(&edit_cfg, settings_status);
  Snapshot tmp;
  if (net_fetch_snapshot(&tmp)) {
    snprintf(settings_status, sizeof(settings_status), "OK HTTP 200");
  } else {
    NetStatus st = net_status();
    snprintf(settings_status, sizeof(settings_status), "%s", st.error[0] ? st.error : "test failed");
  }
  ui_settings(&edit_cfg, settings_status);
}

static void settings_save() {
  config_format_url(&edit_cfg);
  config_parse_url(&edit_cfg);
  config_save(&edit_cfg);
  cfg = edit_cfg;
  ui_set_dark(cfg.dark_mode);
  net_reconfigure(&cfg);
  have_snap = false;
  settings_open = false;
  last_poll = 0;
  redraw();
}

void loop() {
  net_loop();
  apply_ws_events();

  TouchEvent touch = hardware_poll_touch();
  if (settings_open) {
    if (touch.tap) {
      char hit = ui_settings_hit(touch.x, touch.y);
      if (hit == 'h') {
        edit_cfg.use_tls = !edit_cfg.use_tls;
        if (edit_cfg.use_tls && (edit_cfg.port == 80 || edit_cfg.port == 8088)) {
          edit_cfg.port = 8443;
        } else if (!edit_cfg.use_tls && (edit_cfg.port == 443 || edit_cfg.port == 8443)) {
          edit_cfg.port = 8088;
        }
        settings_status[0] = '\0';
        ui_settings(&edit_cfg, settings_status);
      } else if (hit == 'd') {
        edit_cfg.dark_mode = !edit_cfg.dark_mode;
        ui_set_dark(edit_cfg.dark_mode);
        settings_status[0] = '\0';
        ui_settings(&edit_cfg, settings_status);
      } else if (hit == '-' && edit_cfg.port > 1) {
        edit_cfg.port--;
        ui_settings(&edit_cfg, settings_status);
      } else if (hit == '+' && edit_cfg.port < 65535) {
        edit_cfg.port++;
        ui_settings(&edit_cfg, settings_status);
      } else if (hit == 't') {
        settings_test();
      } else if (hit == 's') {
        settings_save();
      } else if (hit == 'b') {
        ui_set_dark(cfg.dark_mode);
        net_reconfigure(&cfg);
        settings_open = false;
        redraw();
      } else if (hit == 'k') {
        provision_enter_token(&edit_cfg, true);
        ui_settings(&edit_cfg, settings_status);
      } else if (hit == 'p') {
        provision_edit_url(&edit_cfg);
        settings_status[0] = '\0';
        ui_settings(&edit_cfg, settings_status);
      }
    }
    delay(20);
    return;
  }

  if (touch.tap) {
    if (overlay_open) {
      close_overlay(true);
      redraw();
    } else {
      NetStatus now = net_status();
      bool online = cfg.demo || now.online;
      if (ui_header_hit(touch.x, touch.y) == 'c') {
        open_settings();
      } else if ((!have_snap || !online) && ui_offline_hit(touch.x, touch.y) == 'e') {
        open_settings();
      } else if (have_snap && online) {
        char hit = ui_page_hit(touch.x, touch.y);
        if (hit == '<') {
          cycle_page(-1);
          redraw();
        } else if (hit == '>') {
          cycle_page(1);
          redraw();
        } else if (hit >= '0' && hit < '0' + UI_PAGE_COUNT) {
          page = static_cast<uint8_t>(hit - '0');
          redraw();
        }
      }
    }
  } else if (touch.swipe_left) {
    cycle_page(1);
    redraw();
  } else if (touch.swipe_right) {
    cycle_page(-1);
    redraw();
  }

  if (overlay_open && millis() > overlay_until) {
    close_overlay(true);
    redraw();
  }

  uint32_t poll_ms = static_cast<uint32_t>(cfg.poll_seconds) * 1000UL;
  NetStatus st = net_status();
  if (cfg.demo) {
    if (millis() - last_poll > 2000) {
      last_poll = millis();
      bool busy = snap.agent.busy;
      fill_demo(&snap);
      ui_note_metrics(snap.host.cpu_pct, snap.host.mem_pct, snap.host.disk_pct);
      if (busy != snap.agent.busy || millis() - last_draw > 2000) {
        redraw();
      }
    }
  } else if (!st.ws && millis() - last_poll >= poll_ms) {
    last_poll = millis();
    Snapshot next;
    if (net_fetch_snapshot(&next)) {
      snap = next;
      have_snap = true;
      last_ok = millis();
      ui_note_metrics(next.host.cpu_pct, next.host.mem_pct, next.host.disk_pct);
      if (next.notify.active) {
        open_overlay(&next.notify);
      } else if (!overlay_open) {
        snap.notify.active = false;
      }
      redraw();
    } else if (have_snap && millis() - last_ok > 15000) {
      redraw();
    } else if (!have_snap) {
      redraw();
    }
  }

  if (millis() - last_heartbeat > 30000) {
    last_heartbeat = millis();
    net_send_heartbeat(hardware_wifi_rssi());
  }

  if (millis() - last_brightness > 2500) {
    last_brightness = millis();
    uint8_t ambient = hardware_auto_brightness();
    uint8_t want = snap.display.brightness > 0 ? snap.display.brightness : ambient;
    hardware_set_brightness(want);
  }

  delay(20);
}
