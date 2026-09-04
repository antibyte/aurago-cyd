#include "provision.h"
#include "protocol.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

bool provision_connect(DeviceConfig *cfg, bool force_portal) {
  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(30);
  wm.setMinimumSignalQuality(8);

  WiFiManagerParameter p_url("url", "AuraGo URL (or demo)", cfg->aurago_url, 127);
  WiFiManagerParameter p_token("token", "Device token (aura_...)", cfg->token, 79);
  wm.addParameter(&p_url);
  wm.addParameter(&p_token);

  wm.setSaveParamsCallback([&]() {
    copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), p_url.getValue());
    copy_trunc(cfg->token, sizeof(cfg->token), p_token.getValue());
    config_parse_url(cfg);
    cfg->demo = config_is_demo(cfg);
    config_save(cfg);
  });

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ap[20];
  snprintf(ap, sizeof(ap), "agocyd-%02X%02X", mac[4], mac[5]);

  bool ok;
  if (force_portal) {
    ok = wm.startConfigPortal(ap);
  } else {
    ok = wm.autoConnect(ap);
  }

  copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), p_url.getValue());
  copy_trunc(cfg->token, sizeof(cfg->token), p_token.getValue());
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
