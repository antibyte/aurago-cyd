#include "provision.h"
#include "audio.h"
#include "protocol.h"
#include "hardware.h"
#include "ui.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void wifi_log_events() {
  static bool events = false;
  if (events) {
    return;
  }
  events = true;
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("wifi: lost reason %u\n", info.wifi_sta_disconnected.reason);
    } else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.printf("wifi: got ip %s\n", WiFi.localIP().toString().c_str());
    }
  });
}

void wifi_apply_hold() {
  // Do not call WiFi.mode() here: switching AP_STA→STA mid-session
  // drops the link. Sleep off + autoreconnect is enough on USB power.
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  esp_wifi_set_ps(WIFI_PS_NONE);
  wifi_log_events();
}

static char g_ap_ssid[20];

static void show_pairing_qr(WiFiManager *wm) {
  (void)wm;
  char qr[48];
  snprintf(qr, sizeof(qr), "WIFI:T:nopass;S:%s;;", g_ap_ssid);
  ui_pairing(g_ap_ssid, qr, WiFi.softAPIP().toString().c_str());
}

static void wifi_use_eu_channels() {
  wifi_country_t country = {};
  memcpy(country.cc, "DE", 2);
  country.schan = 1;
  country.nchan = 13;
  country.max_tx_power = 20;
  country.policy = WIFI_COUNTRY_POLICY_AUTO;
  esp_wifi_set_country(&country);
}

static bool wifi_sta_from_nvs() {
  wifi_config_t conf{};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    Serial.println("wifi: nvs read failed");
    return false;
  }
  char ssid[33];
  char psk[65];
  memset(ssid, 0, sizeof(ssid));
  memset(psk, 0, sizeof(psk));
  memcpy(ssid, conf.sta.ssid, 32);
  memcpy(psk, conf.sta.password, 64);
  Serial.printf("wifi: nvs ssid='%s' psk_len=%u\n", ssid, static_cast<unsigned>(strlen(psk)));
  if (ssid[0] == '\0') {
    return false;
  }
  wifi_use_eu_channels();
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  Serial.printf("wifi: scan %d aps\n", n);
  bool seen = false;
  for (int i = 0; i < n; i++) {
    bool match = strcmp(WiFi.SSID(i).c_str(), ssid) == 0;
    if (match) {
      seen = true;
    }
    if (match || i < 8) {
      Serial.printf("wifi: ap '%s' ch=%d rssi=%d enc=%d%s\n", WiFi.SSID(i).c_str(), WiFi.channel(i),
                    WiFi.RSSI(i), static_cast<int>(WiFi.encryptionType(i)), match ? " *" : "");
    }
  }
  WiFi.scanDelete();
  if (!seen) {
    Serial.printf("wifi: ssid '%s' not in scan\n", ssid);
  }
  // Scan leaves the radio in a funny state; bounce STA before join.
  WiFi.mode(WIFI_OFF);
  delay(120);
  WiFi.mode(WIFI_STA);
  delay(50);
  wifi_use_eu_channels();
  wifi_log_events();
  // Mixed WPA2/WPA3 APs emit AUTH_EXPIRE (reason 2) if the station tries SAE.
  // WiFi.begin() resets authmode, so configure and esp_wifi_connect() instead.
  memset(&conf, 0, sizeof(conf));
  memcpy(conf.sta.ssid, ssid, 32);
  memcpy(conf.sta.password, psk, 64);
  conf.sta.bssid_set = 0;
  conf.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  conf.sta.pmf_cfg.capable = false;
  conf.sta.pmf_cfg.required = false;
  conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
  esp_wifi_set_config(WIFI_IF_STA, &conf);
  delay(50);
  esp_wifi_connect();
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 25000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("wifi: sta %s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  Serial.printf("wifi: sta failed status=%d\n", static_cast<int>(WiFi.status()));
  WiFi.disconnect(false, false);
  delay(200);
  return false;
}

bool provision_connect(DeviceConfig *cfg, bool force_portal) {
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  delay(50);
  wifi_use_eu_channels();
  wifi_log_events();
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(g_ap_ssid, sizeof(g_ap_ssid), "agocyd-%02X%02X", mac[4], mac[5]);

  bool ok = false;
  if (!force_portal) {
    ok = wifi_sta_from_nvs();
  }
  if (!ok) {
    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(20);
    wm.setMinimumSignalQuality(8);

    WiFiManagerParameter p_url("url", "AuraGo URL https://ip:8443", cfg->aurago_url, 127);
    wm.addParameter(&p_url);

    wm.setSaveParamsCallback([&]() {
      copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), p_url.getValue());
      config_parse_url(cfg);
      cfg->demo = config_is_demo(cfg);
      config_save(cfg);
    });
    wm.setAPCallback(show_pairing_qr);

    if (force_portal) {
      Serial.println("wifi: config portal");
      ok = wm.startConfigPortal(g_ap_ssid);
    } else {
      Serial.println("wifi: autoConnect");
      ok = wm.autoConnect(g_ap_ssid);
    }
    copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), p_url.getValue());
  }
  Serial.printf("wifi: done ok=%d status=%d ip=%s\n", ok, static_cast<int>(WiFi.status()),
                WiFi.localIP().toString().c_str());

  if (cfg->aurago_url[0] == '\0') {
    copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), "demo");
  }
  config_parse_url(cfg);
  cfg->demo = config_is_demo(cfg);
  config_save(cfg);

  if (ok && WiFi.status() == WL_CONNECTED) {
    wifi_apply_hold();
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
    audio_loop();
    TouchEvent ev = hardware_poll_touch();
    if (ev.tap) {
      char key = ui_token_key_at(ev.x, ev.y);
      if (key != 0) {
        audio_play(AudioCue::Click);
      }
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
  if (ok && WiFi.status() == WL_CONNECTED) {
    wifi_apply_hold();
  }
  return ok && WiFi.status() == WL_CONNECTED;
}
