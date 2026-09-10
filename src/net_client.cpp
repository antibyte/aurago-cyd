#include "net_client.h"
#include "provision.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <stdio.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.1.0"
#endif
#ifndef CYD_VARIANT
#define CYD_VARIANT "cyd"
#endif

static DeviceConfig cfg;
static NetStatus status;
static WebSocketsClient ws;
static bool ws_started = false;
static uint32_t backoff_ms = 1000;
static uint32_t next_retry_ms = 0;
static uint32_t last_wifi_try = 0;
static bool wifi_was_up = false;

static const int EVENT_QUEUE = 6;
static WsEvent events[EVENT_QUEUE];
static int event_head = 0;
static int event_count = 0;

static char http_buf[2048];
static char ws_headers[112];

static void set_error(const char *msg) {
  copy_trunc(status.error, sizeof(status.error), msg);
}

static void push_event(const WsEvent &ev) {
  int idx = (event_head + event_count) % EVENT_QUEUE;
  if (event_count == EVENT_QUEUE) {
    event_head = (event_head + 1) % EVENT_QUEUE;
    event_count--;
  }
  events[idx] = ev;
  event_count++;
}

static void build_path(char *out, size_t cap, const char *suffix, bool with_token) {
  if (cfg.path_prefix[0] != '\0') {
    snprintf(out, cap, "%s%s", cfg.path_prefix, suffix);
  } else {
    snprintf(out, cap, "%s", suffix);
  }
  if (with_token && cfg.token[0] != '\0') {
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "%stoken=%s", strchr(out, '?') ? "&" : "?", cfg.token);
  }
}

static void build_url(char *out, size_t cap, const char *suffix) {
  const char *scheme = cfg.use_tls ? "https" : "http";
  if (cfg.path_prefix[0] != '\0') {
    snprintf(out, cap, "%s://%s:%u%s%s", scheme, cfg.host, cfg.port, cfg.path_prefix, suffix);
  } else {
    snprintf(out, cap, "%s://%s:%u%s", scheme, cfg.host, cfg.port, suffix);
  }
}

static int http_do(const char *method, const char *suffix, const char *body, char *resp, size_t resp_cap) {
  char url[192];
  build_url(url, sizeof(url), suffix);

  HTTPClient http;
  WiFiClient client;
  WiFiClientSecure secure;
  if (cfg.use_tls) {
    secure.setInsecure();
    if (!http.begin(secure, url)) {
      set_error("http begin failed");
      return -1;
    }
  } else {
    if (!http.begin(client, url)) {
      set_error("http begin failed");
      return -1;
    }
  }
  http.setTimeout(8000);
  http.addHeader("Accept", "application/json");
  if (cfg.token[0] != '\0') {
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg.token);
    http.addHeader("Authorization", auth);
  }
  int code;
  if (strcmp(method, "POST") == 0) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body ? body : "{}");
  } else {
    code = http.GET();
  }
  if (code > 0 && resp != nullptr && resp_cap > 0) {
    String payload = http.getString();
    size_t n = payload.length();
    if (n >= resp_cap) {
      n = resp_cap - 1;
    }
    if (n > 0) {
      memcpy(resp, payload.c_str(), n);
    }
    resp[n] = '\0';
  }
  http.end();
  return code;
}

static void ws_event(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      status.ws = true;
      status.online = true;
      backoff_ms = 1000;
      set_error("");
      break;
    case WStype_DISCONNECTED:
      status.ws = false;
      if (WiFi.status() != WL_CONNECTED) {
        status.online = false;
        set_error("wifi down");
      }
      break;
    case WStype_TEXT: {
      WsEvent ev{};
      if (ws_event_parse(reinterpret_cast<const char *>(payload), length, &ev)) {
        if (ev.type == WsType::Ping) {
          ws.sendTXT("{\"type\":\"pong\"}");
        } else {
          push_event(ev);
        }
        status.online = true;
        status.last_ok_ms = millis();
      }
      break;
    }
    default:
      break;
  }
}

static void ws_start() {
  if (cfg.demo || !cfg.url_ok) {
    return;
  }
  char path[160];
  build_path(path, sizeof(path), "/api/cyd/ws", true);
  if (cfg.token[0] != '\0') {
    snprintf(ws_headers, sizeof(ws_headers), "Authorization: Bearer %s", cfg.token);
    ws.setExtraHeaders(ws_headers);
  }
  ws.onEvent(ws_event);
  ws.setReconnectInterval(5000);
  if (cfg.use_tls) {
    ws.beginSSL(cfg.host, cfg.port, path);
  } else {
    ws.begin(cfg.host, cfg.port, path);
  }
  ws_started = true;
}

void net_begin(const DeviceConfig *in) {
  cfg = *in;
  memset(&status, 0, sizeof(status));
  event_head = 0;
  event_count = 0;
  backoff_ms = 1000;
  next_retry_ms = 0;
  status.wifi = WiFi.status() == WL_CONNECTED;
  wifi_was_up = status.wifi;
  last_wifi_try = millis();
  if (status.wifi) {
    wifi_apply_hold();
  }
  if (!cfg.demo && cfg.url_ok) {
    ws_start();
  }
}

void net_reconfigure(const DeviceConfig *in) {
  if (ws_started) {
    ws.disconnect();
    ws_started = false;
  }
  net_begin(in);
}

void net_loop() {
  wl_status_t st = WiFi.status();
  bool up = st == WL_CONNECTED;
  status.wifi = up;
  if (!up) {
    status.online = false;
    status.ws = false;
    if (status.error[0] == '\0') {
      set_error("wifi down");
    }
    uint32_t now = millis();
    // last_wifi_try starts at net_begin, so the first retry waits a full
    // interval. Calling reconnect() during association aborts the join.
    if (now - last_wifi_try >= 8000) {
      last_wifi_try = now;
      if (st == WL_DISCONNECTED || st == WL_CONNECTION_LOST || st == WL_CONNECT_FAILED ||
          st == WL_NO_SSID_AVAIL) {
        Serial.printf("wifi: reconnect status=%d\n", static_cast<int>(st));
        WiFi.setAutoReconnect(true);
        WiFi.reconnect();
      }
    }
  } else if (!wifi_was_up) {
    wifi_apply_hold();
    set_error("");
    backoff_ms = 1000;
    next_retry_ms = 0;
    if (!ws_started && !cfg.demo && cfg.url_ok) {
      ws_start();
    }
    Serial.printf("wifi: up %s\n", WiFi.localIP().toString().c_str());
  }
  wifi_was_up = up;
  if (ws_started) {
    ws.loop();
  }
}

bool net_fetch_snapshot(Snapshot *out) {
  if (cfg.demo) {
    set_error("");
    status.online = true;
    status.last_ok_ms = millis();
    return false;
  }
  if (!status.wifi) {
    set_error("wifi down");
    status.online = false;
    return false;
  }
  if (millis() < next_retry_ms) {
    return false;
  }

  int code = http_do("GET", "/api/cyd/snapshot", nullptr, http_buf, sizeof(http_buf));
  if (code != 200) {
    char err[64];
    snprintf(err, sizeof(err), "snapshot HTTP %d", code);
    set_error(err);
    status.online = false;
    backoff_ms = backoff_ms < 30000 ? backoff_ms * 2 : 30000;
    next_retry_ms = millis() + backoff_ms;
    return false;
  }
  if (!snapshot_parse(http_buf, strlen(http_buf), out)) {
    const char *why = snapshot_parse_error();
    char err[64];
    snprintf(err, sizeof(err), "json %s", why && why[0] ? why : "parse");
    set_error(err);
    Serial.printf("snapshot parse failed (%s): %.80s\n", err, http_buf);
    return false;
  }
  status.online = true;
  status.last_ok_ms = millis();
  backoff_ms = 1000;
  next_retry_ms = 0;
  set_error("");
  return true;
}

bool net_fetch_speak(const char *id, uint8_t *dst, size_t cap, size_t *out_n) {
  if (out_n != nullptr) {
    *out_n = 0;
  }
  if (id == nullptr || id[0] == '\0' || dst == nullptr || cap < 16) {
    return false;
  }
  if (!status.wifi) {
    return false;
  }
  char suffix[80];
  snprintf(suffix, sizeof(suffix), "/api/cyd/speak/%s", id);
  char url[192];
  build_url(url, sizeof(url), suffix);

  HTTPClient http;
  WiFiClient client;
  WiFiClientSecure secure;
  if (cfg.use_tls) {
    secure.setInsecure();
    if (!http.begin(secure, url)) {
      return false;
    }
  } else {
    if (!http.begin(client, url)) {
      return false;
    }
  }
  http.setTimeout(10000);
  http.addHeader("Accept", "application/octet-stream");
  if (cfg.token[0] != '\0') {
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg.token);
    http.addHeader("Authorization", auth);
  }
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  int len = http.getSize();
  String payload = http.getString();
  http.end();
  size_t n = payload.length();
  if (len > 0 && static_cast<size_t>(len) < n) {
    n = static_cast<size_t>(len);
  }
  if (n == 0) {
    return false;
  }
  if (n > cap) {
    n = cap;
  }
  memcpy(dst, payload.c_str(), n);
  if (out_n != nullptr) {
    *out_n = n;
  }
  return true;
}

bool net_send_heartbeat(int8_t rssi) {
  if (cfg.demo || !cfg.url_ok || !status.wifi) {
    return false;
  }
  char body[160];
  snprintf(body, sizeof(body),
           "{\"firmware\":\"%s\",\"variant\":\"%s\",\"rssi\":%d,\"width\":320,\"height\":240}",
           FIRMWARE_VERSION, CYD_VARIANT, static_cast<int>(rssi));
  int code = http_do("POST", "/api/cyd/heartbeat", body, nullptr, 0);
  return code >= 200 && code < 300;
}

bool net_send_ack(const char *id) {
  if (cfg.demo || id == nullptr || id[0] == '\0') {
    return true;
  }
  char body[96];
  snprintf(body, sizeof(body), "{\"id\":\"%s\",\"action\":\"dismiss\"}", id);
  if (status.ws) {
    char msg[128];
    snprintf(msg, sizeof(msg), "{\"type\":\"ack\",\"id\":\"%s\",\"action\":\"dismiss\"}", id);
    ws.sendTXT(msg);
  }
  int code = http_do("POST", "/api/cyd/ack", body, nullptr, 0);
  return (code >= 200 && code < 300) || status.ws;
}

void net_ws_pong() {
  if (status.ws) {
    ws.sendTXT("{\"type\":\"pong\"}");
  }
}

NetStatus net_status() {
  status.wifi = WiFi.status() == WL_CONNECTED;
  return status;
}

bool net_take_event(WsEvent *out) {
  if (event_count == 0 || out == nullptr) {
    return false;
  }
  *out = events[event_head];
  event_head = (event_head + 1) % EVENT_QUEUE;
  event_count--;
  return true;
}
