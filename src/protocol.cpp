#include "protocol.h"

#include <ArduinoJson.h>
#include <string.h>
#include <strings.h>

void copy_trunc(char *dst, size_t cap, const char *src) {
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

void snapshot_clear(Snapshot *s) {
  if (s == nullptr) {
    return;
  }
  memset(s, 0, sizeof(*s));
  copy_trunc(s->display.page, sizeof(s->display.page), "status");
  copy_trunc(s->display.led, sizeof(s->display.led), "green");
  s->display.brightness = 180;
}

static void parse_notify(JsonVariantConst n, NotifyInfo *out) {
  memset(out, 0, sizeof(*out));
  if (n.isNull() || !n.is<JsonObject>()) {
    return;
  }
  out->active = true;
  copy_trunc(out->id, sizeof(out->id), n["id"] | "");
  copy_trunc(out->title, sizeof(out->title), n["title"] | "");
  copy_trunc(out->body, sizeof(out->body), n["body"] | "");
  copy_trunc(out->priority, sizeof(out->priority), n["priority"] | "normal");
  int ttl = n["ttl_s"] | 0;
  if (ttl < 0) {
    ttl = 0;
  }
  if (ttl > 300) {
    ttl = 300;
  }
  out->ttl_s = static_cast<uint16_t>(ttl);
}

static bool parse_snapshot_object(JsonVariantConst root, Snapshot *out) {
  if (!root.is<JsonObject>()) {
    return false;
  }
  snapshot_clear(out);
  out->ts = root["ts"] | 0u;

  JsonVariantConst agent = root["agent"];
  out->agent.busy = agent["busy"] | false;
  copy_trunc(out->agent.model, sizeof(out->agent.model), agent["model"] | "");
  copy_trunc(out->agent.personality, sizeof(out->agent.personality), agent["personality"] | "");
  copy_trunc(out->agent.task, sizeof(out->agent.task), agent["task"] | "");

  JsonVariantConst host = root["host"];
  out->host.cpu_pct = host["cpu_pct"] | 0.0f;
  out->host.mem_pct = host["mem_pct"] | 0.0f;
  out->host.disk_pct = host["disk_pct"] | 0.0f;
  out->host.uptime_s = host["uptime_s"] | 0u;
  out->host.host_uptime_s = host["host_uptime_s"] | 0u;

  JsonVariantConst work = root["work"];
  out->work.missions_running = work["missions_running"] | 0;
  out->work.missions_queued = work["missions_queued"] | 0;
  out->work.notes_open = work["notes_open"] | 0;
  out->work.last_user_h = work["last_user_h"] | -1.0f;

  JsonVariantConst display = root["display"];
  copy_trunc(out->display.page, sizeof(out->display.page), display["page"] | "status");
  int brightness = display["brightness"] | 180;
  if (brightness < 0) {
    brightness = 0;
  }
  if (brightness > 255) {
    brightness = 255;
  }
  out->display.brightness = static_cast<uint8_t>(brightness);
  copy_trunc(out->display.led, sizeof(out->display.led), display["led"] | "green");

  parse_notify(root["notify"], &out->notify);
  return true;
}

bool snapshot_parse(const char *json, size_t len, Snapshot *out) {
  if (json == nullptr || out == nullptr || len == 0) {
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    return false;
  }
  return parse_snapshot_object(doc.as<JsonVariantConst>(), out);
}

bool ws_event_parse(const char *json, size_t len, WsEvent *out) {
  if (json == nullptr || out == nullptr || len == 0) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->type = WsType::Unknown;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    return false;
  }

  const char *type = doc["type"] | "";
  if (strcasecmp(type, "snapshot") == 0) {
    out->type = WsType::Snapshot;
    JsonVariantConst data = doc["data"];
    if (data.isNull()) {
      return parse_snapshot_object(doc.as<JsonVariantConst>(), &out->snapshot);
    }
    return parse_snapshot_object(data, &out->snapshot);
  }
  if (strcasecmp(type, "notify") == 0) {
    out->type = WsType::Notify;
    parse_notify(doc.as<JsonVariantConst>(), &out->notify);
    out->notify.active = true;
    copy_trunc(out->id, sizeof(out->id), out->notify.id);
    return out->notify.id[0] != '\0' || out->notify.body[0] != '\0' || out->notify.title[0] != '\0';
  }
  if (strcasecmp(type, "clear") == 0) {
    out->type = WsType::Clear;
    copy_trunc(out->id, sizeof(out->id), doc["id"] | "");
    return true;
  }
  if (strcasecmp(type, "led") == 0) {
    out->type = WsType::Led;
    copy_trunc(out->led, sizeof(out->led), doc["color"] | doc["led"] | "green");
    return true;
  }
  if (strcasecmp(type, "page") == 0) {
    out->type = WsType::Page;
    copy_trunc(out->page, sizeof(out->page), doc["page"] | "status");
    return true;
  }
  if (strcasecmp(type, "ping") == 0) {
    out->type = WsType::Ping;
    return true;
  }
  return false;
}

int notify_rank(const char *priority) {
  if (priority == nullptr) {
    return 1;
  }
  if (strcasecmp(priority, "critical") == 0) {
    return 3;
  }
  if (strcasecmp(priority, "high") == 0) {
    return 2;
  }
  if (strcasecmp(priority, "low") == 0) {
    return 0;
  }
  return 1;
}

uint16_t notify_ttl(const NotifyInfo *n) {
  if (n == nullptr) {
    return 30;
  }
  if (n->ttl_s > 0) {
    return n->ttl_s;
  }
  if (notify_rank(n->priority) >= 3) {
    return 60;
  }
  return 30;
}
