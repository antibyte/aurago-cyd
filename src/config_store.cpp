#include "config_store.h"

#include <Preferences.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static Preferences prefs;

static void copy_trunc_local(char *dst, size_t cap, const char *src) {
  if (dst == nullptr || cap == 0) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  size_t n = 0;
  while (src[n] != '\0' && n + 1 < cap) {
    dst[n] = src[n];
    n++;
  }
  dst[n] = '\0';
}

static void copy_field(char *dst, size_t cap, const String &src) {
  copy_trunc_local(dst, cap, src.c_str());
}

void config_load(DeviceConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->port = 8088;
  cfg->poll_seconds = 5;
  copy_trunc_local(cfg->aurago_url, sizeof(cfg->aurago_url), "demo");

  if (!prefs.begin("aurago-cyd", true)) {
    cfg->demo = true;
    return;
  }
  String url = prefs.getString("url", "demo");
  String token = prefs.getString("token", "");
  uint8_t poll = prefs.getUChar("poll", 5);
  prefs.end();

  copy_field(cfg->aurago_url, sizeof(cfg->aurago_url), url);
  copy_field(cfg->token, sizeof(cfg->token), token);
  cfg->poll_seconds = poll < 2 ? 2 : poll;
  config_parse_url(cfg);
  cfg->demo = config_is_demo(cfg);
}

void config_save(const DeviceConfig *cfg) {
  if (cfg == nullptr) {
    return;
  }
  if (!prefs.begin("aurago-cyd", false)) {
    return;
  }
  prefs.putString("url", cfg->aurago_url);
  prefs.putString("token", cfg->token);
  prefs.putUChar("poll", cfg->poll_seconds);
  prefs.end();
}

void config_clear() {
  if (prefs.begin("aurago-cyd", false)) {
    prefs.clear();
    prefs.end();
  }
}

static const char kTokenAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

void config_normalize_token(char *dst, size_t cap, const char *src) {
  if (dst == nullptr || cap == 0) {
    return;
  }
  dst[0] = '\0';
  if (src == nullptr) {
    return;
  }
  char compact[80];
  size_t n = 0;
  for (const char *p = src; *p != '\0' && n + 1 < sizeof(compact); p++) {
    if (*p == ' ' || *p == '-' || *p == '\t' || *p == '\n') {
      continue;
    }
    compact[n++] = *p;
  }
  compact[n] = '\0';
  if (n == 0) {
    return;
  }
  const char *body = compact;
  if (n >= 5 && strncasecmp(compact, "aura_", 5) == 0) {
    body = compact + 5;
  }
  size_t body_len = strlen(body);
  if (body_len == 9) {
    if (cap < 15) {
      return;
    }
    memcpy(dst, "aura_", 5);
    for (size_t i = 0; i < 9; i++) {
      char c = body[i];
      if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 32);
      }
      dst[5 + i] = c;
    }
    dst[14] = '\0';
    return;
  }
  snprintf(dst, cap, "aura_%s", body);
}

bool config_token_ready(const char *token) {
  char norm[80];
  config_normalize_token(norm, sizeof(norm), token ? token : "");
  if (strncmp(norm, "aura_", 5) != 0) {
    return false;
  }
  const char *body = norm + 5;
  size_t n = strlen(body);
  if (n == 32) {
    return true;
  }
  if (n != 9) {
    return false;
  }
  for (size_t i = 0; i < 9; i++) {
    if (strchr(kTokenAlphabet, body[i]) == nullptr) {
      return false;
    }
  }
  return true;
}

bool config_is_demo(const DeviceConfig *cfg) {
  if (cfg == nullptr) {
    return true;
  }
  if (cfg->aurago_url[0] == '\0') {
    return true;
  }
  return strcasecmp(cfg->aurago_url, "demo") == 0;
}

bool config_parse_url(DeviceConfig *cfg) {
  if (cfg == nullptr) {
    return false;
  }
  cfg->url_ok = false;
  cfg->use_tls = false;
  cfg->port = 8088;
  cfg->host[0] = '\0';
  cfg->path_prefix[0] = '\0';

  if (config_is_demo(cfg)) {
    cfg->demo = true;
    return true;
  }

  const char *url = cfg->aurago_url;
  const char *rest = url;
  if (strncasecmp(url, "https://", 8) == 0) {
    cfg->use_tls = true;
    cfg->port = 443;
    rest = url + 8;
  } else if (strncasecmp(url, "http://", 7) == 0) {
    cfg->use_tls = false;
    cfg->port = 8088;
    rest = url + 7;
  } else if (strchr(url, '.') != nullptr || strchr(url, ':') != nullptr) {
    cfg->use_tls = false;
    cfg->port = 8088;
    rest = url;
  } else {
    return false;
  }

  const char *slash = strchr(rest, '/');
  const char *colon = strchr(rest, ':');
  size_t host_len;
  if (colon != nullptr && (slash == nullptr || colon < slash)) {
    host_len = static_cast<size_t>(colon - rest);
    cfg->port = static_cast<uint16_t>(atoi(colon + 1));
    if (cfg->port == 0) {
      cfg->port = cfg->use_tls ? 443 : 80;
    }
  } else {
    host_len = slash ? static_cast<size_t>(slash - rest) : strlen(rest);
    if (!cfg->use_tls) {
      cfg->port = 80;
    }
  }
  if (host_len == 0 || host_len >= sizeof(cfg->host)) {
    return false;
  }
  memcpy(cfg->host, rest, host_len);
  cfg->host[host_len] = '\0';

  if (slash != nullptr && slash[1] != '\0') {
    copy_trunc_local(cfg->path_prefix, sizeof(cfg->path_prefix), slash);
    size_t n = strlen(cfg->path_prefix);
    while (n > 0 && cfg->path_prefix[n - 1] == '/') {
      cfg->path_prefix[n - 1] = '\0';
      n--;
    }
  }

  cfg->url_ok = cfg->host[0] != '\0';
  cfg->demo = false;
  return cfg->url_ok;
}
