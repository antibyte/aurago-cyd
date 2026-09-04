#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PROTO_TITLE_MAX 32
#define PROTO_BODY_MAX 96
#define PROTO_TASK_MAX 40
#define PROTO_MODEL_MAX 23
#define PROTO_ID_MAX 39
#define PROTO_LED_MAX 7
#define PROTO_PAGE_MAX 11
#define PROTO_PRIO_MAX 11

struct AgentInfo {
  bool busy;
  char model[PROTO_MODEL_MAX + 1];
  char personality[PROTO_MODEL_MAX + 1];
  char task[PROTO_TASK_MAX + 1];
};

struct HostMetrics {
  float cpu_pct;
  float mem_pct;
  float disk_pct;
  uint32_t uptime_s;
  uint32_t host_uptime_s;
};

struct WorkInfo {
  int missions_running;
  int missions_queued;
  int notes_open;
  float last_user_h;
};

struct DisplayInfo {
  char page[PROTO_PAGE_MAX + 1];
  uint8_t brightness;
  char led[PROTO_LED_MAX + 1];
};

struct NotifyInfo {
  bool active;
  char id[PROTO_ID_MAX + 1];
  char title[PROTO_TITLE_MAX + 1];
  char body[PROTO_BODY_MAX + 1];
  char priority[PROTO_PRIO_MAX + 1];
  uint16_t ttl_s;
};

struct Snapshot {
  uint32_t ts;
  AgentInfo agent;
  HostMetrics host;
  WorkInfo work;
  DisplayInfo display;
  NotifyInfo notify;
};

enum class WsType {
  Snapshot,
  Notify,
  Clear,
  Led,
  Page,
  Ping,
  Unknown
};

struct WsEvent {
  WsType type;
  Snapshot snapshot;
  NotifyInfo notify;
  char id[PROTO_ID_MAX + 1];
  char led[PROTO_LED_MAX + 1];
  char page[PROTO_PAGE_MAX + 1];
};

void snapshot_clear(Snapshot *s);
bool snapshot_parse(const char *json, size_t len, Snapshot *out);
bool ws_event_parse(const char *json, size_t len, WsEvent *out);
int notify_rank(const char *priority);
uint16_t notify_ttl(const NotifyInfo *n);
void copy_trunc(char *dst, size_t cap, const char *src);
