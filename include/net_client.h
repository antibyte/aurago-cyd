#pragma once

#include "config_store.h"
#include "protocol.h"

struct NetStatus {
  bool wifi;
  bool online;
  bool ws;
  uint32_t last_ok_ms;
  char error[64];
};

void net_begin(const DeviceConfig *cfg);
void net_loop();
bool net_fetch_snapshot(Snapshot *out);
bool net_send_heartbeat(int8_t rssi);
bool net_send_ack(const char *id);
void net_ws_pong();
NetStatus net_status();
bool net_take_event(WsEvent *out);
