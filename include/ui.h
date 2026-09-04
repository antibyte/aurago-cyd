#pragma once

#include "protocol.h"
#include "net_client.h"

void ui_begin();
void ui_splash(const char *line1, const char *line2 = nullptr);
void ui_offline(const NetStatus *st, uint32_t last_ok_ms);
void ui_render(const Snapshot *snap, uint8_t page, bool online, int8_t rssi);
void ui_overlay(const NotifyInfo *n, uint32_t remain_ms);
