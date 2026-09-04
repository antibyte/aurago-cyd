#include "net_client.h"

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
    WiFiClient *stream = http.getStreamPtr();
    int n = 0;
    if (stream != nullptr) {
      n = stream->readBytes(resp, resp_cap - 1);
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
  status.wifi = WiFi.status() == WL_CONNECTED;
  if (!cfg.demo && cfg.url_ok) {
    ws_start();
  }
}

void net_loop() {
  status.wifi = WiFi.status() == WL_CONNECTED;
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
    set_error("bad snapshot json");
    return false;
  }
  status.online = true;
  status.last_ok_ms = millis();
  backoff_ms = 1000;
  next_retry_ms = 0;
  set_error("");
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
