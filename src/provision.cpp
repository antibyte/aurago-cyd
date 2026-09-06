#include "provision.h"
#include "protocol.h"
#include "hardware.h"
#include "ui.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static char g_ap_ssid[20];

static void show_pairing_qr(WiFiManager *wm) {
  (void)wm;
  char qr[48];
  snprintf(qr, sizeof(qr), "WIFI:T:nopass;S:%s;;", g_ap_ssid);
  ui_pairing(g_ap_ssid, qr, WiFi.softAPIP().toString().c_str());
}

bool provision_connect(DeviceConfig *cfg, bool force_portal) {
  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(30);
  wm.setMinimumSignalQuality(8);

  WiFiManagerParameter p_url("url", "AuraGo URL https://ip:8443", cfg->aurago_url, 127);
  wm.addParameter(&p_url);

  wm.setSaveParamsCallback([&]() {
    copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), p_url.getValue());
    config_parse_url(cfg);
    cfg->demo = config_is_demo(cfg);
    config_save(cfg);
  });

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(g_ap_ssid, sizeof(g_ap_ssid), "agocyd-%02X%02X", mac[4], mac[5]);
  wm.setAPCallback(show_pairing_qr);

  bool ok;
  if (force_portal) {
    ok = wm.startConfigPortal(g_ap_ssid);
  } else {
    ok = wm.autoConnect(g_ap_ssid);
  }

  copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), p_url.getValue());
  if (cfg->aurago_url[0] == '\0') {
    copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), "demo");
  }
  config_parse_url(cfg);
  cfg->demo = config_is_demo(cfg);
  config_save(cfg);

  if (ok && WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
  }
  return ok && WiFi.status() == WL_CONNECTED;
}

bool provision_enter_token(DeviceConfig *cfg, bool force) {
  if (cfg == nullptr) {
    return false;
  }
  if (config_is_demo(cfg) || (!force && config_token_ready(cfg->token))) {
    return true;
  }

  char body[10];
  memset(body, 0, sizeof(body));
  int n = 0;
  ui_token_entry("aura_", body);

  for (;;) {
    TouchEvent ev = hardware_poll_touch();
    if (ev.tap) {
      char key = ui_token_key_at(ev.x, ev.y);
      if (key == '\b') {
        if (n > 0) {
          body[--n] = '\0';
        }
      } else if (key == '\n') {
        if (n == 9) {
          break;
        }
      } else if (key != 0 && n < 9) {
        body[n++] = key;
        body[n] = '\0';
      }
      ui_token_entry("aura_", body);
    }
    delay(20);
  }

  char raw[20];
  snprintf(raw, sizeof(raw), "aura_%s", body);
  config_normalize_token(cfg->token, sizeof(cfg->token), raw);
  config_save(cfg);
  return config_token_ready(cfg->token);
}

bool provision_edit_url(DeviceConfig *cfg) {
  if (cfg == nullptr) {
    return false;
  }
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(g_ap_ssid, sizeof(g_ap_ssid), "agocyd-%02X%02X", mac[4], mac[5]);

  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(180);
  WiFiManagerParameter p_url("url", "AuraGo URL https://ip:8443", cfg->aurago_url, 127);
  wm.addParameter(&p_url);
  wm.setAPCallback(show_pairing_qr);
  wm.setSaveParamsCallback([&]() {
    copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), p_url.getValue());
    config_parse_url(cfg);
    cfg->demo = config_is_demo(cfg);
    config_save(cfg);
  });
  bool ok = wm.startConfigPortal(g_ap_ssid);
  copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), p_url.getValue());
  if (cfg->aurago_url[0] == '\0') {
    copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), "demo");
  }
  config_parse_url(cfg);
  cfg->demo = config_is_demo(cfg);
  config_save(cfg);
  return ok && WiFi.status() == WL_CONNECTED;
}
