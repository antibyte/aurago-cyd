#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "audio.h"
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
static uint32_t last_touch = 0;
static uint32_t last_page_ms = 0;
static bool carousel_on = false;
static bool have_snap = false;
static LedColor forced_led = LedColor::Off;
static bool have_forced_led = false;
static bool settings_open = false;
static DeviceConfig edit_cfg;
static char settings_status[48];
static char speak_id[PROTO_ID_MAX + 1];
static uint32_t speak_at = 0;
static uint8_t speak_pcm[24000];

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
  s->alerts.count = 2;
  s->alerts.n = 2;
  copy_trunc(s->alerts.items[0].sev, sizeof(s->alerts.items[0].sev), "warning");
  copy_trunc(s->alerts.items[0].title, sizeof(s->alerts.items[0].title), "disk 90%");
  copy_trunc(s->alerts.items[1].sev, sizeof(s->alerts.items[1].sev), "info");
  copy_trunc(s->alerts.items[1].title, sizeof(s->alerts.items[1].title), "vpn ok");
  s->mesh.unread = 1;
  s->mesh.n = 1;
  copy_trunc(s->mesh.items[0].title, sizeof(s->mesh.items[0].title), "Alice");
  copy_trunc(s->mesh.items[0].body, sizeof(s->mesh.items[0].body), "ping");
  s->mesh.items[0].age_s = 12;
}

static bool overlay_replace_ok(const NotifyInfo *incoming) {
  if (!overlay_open || !snap.notify.active) {
    return true;
  }
  return notify_rank(incoming->priority) >= notify_rank(snap.notify.priority);
}

static void note_touch() {
  last_touch = millis();
  last_page_ms = millis();
  carousel_on = false;
}

static bool open_overlay(const NotifyInfo *n) {
  if (n == nullptr || !n->active) {
    return false;
  }
  if (!overlay_replace_ok(n)) {
    return false;
  }
  bool repeat = overlay_open && n->id[0] != '\0' && strcmp(snap.notify.id, n->id) == 0;
  snap.notify = *n;
  snap.notify.active = true;
  overlay_open = true;
  overlay_until = millis() + static_cast<uint32_t>(notify_ttl(n)) * 1000UL;
  note_touch();
  if (repeat) {
    return false;
  }
  if (strncasecmp(n->title, "Mesh", 4) == 0) {
    audio_play(AudioCue::Mesh);
  } else if (notify_rank(n->priority) >= 3) {
    audio_play(AudioCue::NotifyHigh);
  } else {
    audio_play(AudioCue::Notify);
  }
  if (n->speak && n->id[0] != '\0') {
    copy_trunc(speak_id, sizeof(speak_id), n->id);
    speak_at = millis() + 600;
  }
  return true;
}

static void play_feed_sounds(int prev_alerts, int prev_unread, bool chimed) {
  if (chimed || prev_alerts < 0) {
    return;
  }
  if (snap.alerts.count > prev_alerts) {
    audio_play(AudioCue::Alert);
  } else if (snap.mesh.unread > prev_unread) {
    audio_play(AudioCue::Mesh);
  }
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
    uint32_t remain = overlay_until > millis() ? overlay_until - millis() : 0;
    ui_render(&snap, page, online, hardware_wifi_rssi(), cfg.dark_mode,
              overlay_open && snap.notify.active ? &snap.notify : nullptr, remain);
  }
  apply_led();
  last_draw = millis();
}

static void apply_ws_events() {
  WsEvent ev;
  bool dirty = false;
  while (net_take_event(&ev)) {
    switch (ev.type) {
      case WsType::Snapshot: {
        // Refresh metrics only. Page jumps come from WsType::Page / Notify,
        // otherwise a stuck hub display.page (alerts/mesh) steals the glass.
        int prev_alerts = have_snap ? snap.alerts.count : -1;
        int prev_unread = have_snap ? snap.mesh.unread : -1;
        NotifyInfo keep{};
        bool keep_overlay = overlay_open && snap.notify.active;
        if (keep_overlay) {
          keep = snap.notify;
        }
        snap = ev.snapshot;
        have_snap = true;
        last_ok = millis();
        ui_note_metrics(snap.host.cpu_pct, snap.host.mem_pct, snap.host.disk_pct);
        bool chimed = false;
        if (snap.notify.active) {
          chimed = open_overlay(&snap.notify);
        } else if (keep_overlay) {
          snap.notify = keep;
        }
        play_feed_sounds(prev_alerts, prev_unread, chimed);
        dirty = true;
        break;
      }
      case WsType::Notify:
        open_overlay(&ev.notify);
        if (!settings_open && strncasecmp(ev.notify.title, "Mesh", 4) == 0) {
          page = 4;
        }
        if (!settings_open) {
          note_touch();
        }
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
        if (!settings_open) {
          page = ui_page_from_name(ev.page);
          note_touch();
          dirty = true;
        }
        break;
      default:
        break;
    }
  }
  if (dirty && !settings_open) {
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
  audio_set_volume(cfg.volume);

  bool force_portal = hardware_boot_held(5000);
  Serial.printf("wifi: boot_portal=%d\n", force_portal ? 1 : 0);
  if (force_portal) {
    ui_splash("config reset", "join agocyd-XXXX");
    config_clear();
    config_load(&cfg);
    ui_set_dark(cfg.dark_mode);
    audio_set_volume(cfg.volume);
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
  last_touch = millis();
  last_page_ms = millis();
  carousel_on = false;
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
  last_page_ms = millis();
}

static void open_settings() {
  note_touch();
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
  audio_set_volume(cfg.volume);
  net_reconfigure(&cfg);
  have_snap = false;
  settings_open = false;
  last_poll = 0;
  note_touch();
  redraw();
}

void loop() {
  static bool ui_wifi = true;
  net_loop();
  apply_ws_events();
  audio_loop();
  if (speak_id[0] != '\0' && !settings_open && !audio_busy() && millis() >= speak_at) {
    size_t n = 0;
    bool got = false;
    for (int i = 0; i < 16 && !got; i++) {
      if (net_fetch_speak(speak_id, speak_pcm, sizeof(speak_pcm), &n) && n > 16) {
        audio_play_pcm_u8(speak_pcm, static_cast<uint16_t>(n), 8000);
        got = true;
      } else {
        delay(250);
      }
    }
    speak_id[0] = '\0';
  }
  bool wifi_now = WiFi.status() == WL_CONNECTED;
  if (wifi_now != ui_wifi && !settings_open) {
    redraw();
  }
  ui_wifi = wifi_now;

  TouchEvent touch = hardware_poll_touch();
  if (settings_open) {
    if (touch.tap) {
      note_touch();
      char hit = ui_settings_hit(touch.x, touch.y);
      if (hit != 0 && hit != 'q' && hit != 'u' && hit != 'm') {
        audio_play(AudioCue::Click);
      }
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
      } else if (hit == 'q') {
        if (edit_cfg.volume > 0) {
          edit_cfg.volume--;
        }
        audio_set_volume(edit_cfg.volume);
        audio_play(AudioCue::Click);
        settings_status[0] = '\0';
        ui_settings(&edit_cfg, settings_status);
      } else if (hit == 'u') {
        if (edit_cfg.volume < AUDIO_VOL_MAX) {
          edit_cfg.volume++;
        }
        audio_set_volume(edit_cfg.volume);
        audio_play(AudioCue::Click);
        settings_status[0] = '\0';
        ui_settings(&edit_cfg, settings_status);
      } else if (hit == 'm') {
        static uint8_t vol_restore = 7;
        if (edit_cfg.volume == 0) {
          edit_cfg.volume = vol_restore == 0 ? 7 : vol_restore;
        } else {
          vol_restore = edit_cfg.volume;
          edit_cfg.volume = 0;
        }
        audio_set_volume(edit_cfg.volume);
        audio_play(AudioCue::Click);
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
        audio_set_volume(cfg.volume);
        net_reconfigure(&cfg);
        settings_open = false;
        note_touch();
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

  if (touch.tap || touch.swipe_left || touch.swipe_right) {
    note_touch();
  }

  if (touch.tap) {
    if (overlay_open) {
      audio_play(AudioCue::Click);
      close_overlay(true);
      redraw();
    } else {
      NetStatus now = net_status();
      bool online = cfg.demo || now.online;
      char head = ui_header_hit(touch.x, touch.y);
      if (head == 'c') {
        audio_play(AudioCue::Click);
        open_settings();
      } else if (head == 'a' && have_snap && online) {
        audio_play(AudioCue::Click);
        page = 3;
        redraw();
      } else if (head == 'm' && have_snap && online) {
        audio_play(AudioCue::Click);
        page = 4;
        redraw();
      } else if ((!have_snap || !online) && ui_offline_hit(touch.x, touch.y) == 'e') {
        audio_play(AudioCue::Click);
        open_settings();
      } else if (have_snap && online) {
        char hit = ui_page_hit(touch.x, touch.y);
        if (hit == '<') {
          audio_play(AudioCue::Click);
          cycle_page(-1);
          redraw();
        } else if (hit == '>') {
          audio_play(AudioCue::Click);
          cycle_page(1);
          redraw();
        } else if (hit >= '0' && hit < '0' + UI_PAGE_COUNT) {
          audio_play(AudioCue::Click);
          page = static_cast<uint8_t>(hit - '0');
          redraw();
        }
      }
    }
  } else if (touch.swipe_left) {
    audio_play(AudioCue::Swipe);
    cycle_page(1);
    redraw();
  } else if (touch.swipe_right) {
    audio_play(AudioCue::Swipe);
    cycle_page(-1);
    redraw();
  }

  if (!settings_open && !overlay_open && have_snap) {
    uint32_t nowms = millis();
    if (!carousel_on) {
      if (nowms - last_touch >= UI_IDLE_MS) {
        // Arm without advancing: the current page (often Home) still gets a
        // full UI_ROTATE_MS dwell. Advancing here skipped that slot because
        // last_touch is already UI_IDLE_MS old.
        carousel_on = true;
        last_page_ms = nowms;
      }
    } else if (nowms - last_page_ms >= UI_ROTATE_MS) {
      cycle_page(1);
      redraw();
    }
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
      int prev_alerts = have_snap ? snap.alerts.count : -1;
      int prev_unread = have_snap ? snap.mesh.unread : -1;
      NotifyInfo keep{};
      bool keep_overlay = overlay_open && snap.notify.active;
      if (keep_overlay) {
        keep = snap.notify;
      }
      snap = next;
      have_snap = true;
      last_ok = millis();
      ui_note_metrics(next.host.cpu_pct, next.host.mem_pct, next.host.disk_pct);
      bool chimed = false;
      if (next.notify.active) {
        chimed = open_overlay(&next.notify);
      } else if (keep_overlay) {
        snap.notify = keep;
      } else {
        snap.notify.active = false;
      }
      play_feed_sounds(prev_alerts, prev_unread, chimed);
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
