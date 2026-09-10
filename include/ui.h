#pragma once

#include <stdint.h>

#include "protocol.h"
#include "net_client.h"

#define UI_PAGE_COUNT 5
#define UI_IDLE_MS 10000
#define UI_ROTATE_MS 5000

void ui_begin();
void ui_set_dark(bool dark);
void ui_note_metrics(float cpu, float mem, float disk);
void ui_set_link_info(const char *ip);
uint8_t ui_page_from_name(const char *name);
char ui_header_hit(int16_t x, int16_t y);
char ui_page_hit(int16_t x, int16_t y);
void ui_splash(const char *line1, const char *line2 = nullptr);
void ui_pairing(const char *ssid, const char *qr_text, const char *portal_ip);
void ui_token_entry(const char *prefix, const char *body);
char ui_token_key_at(int16_t x, int16_t y);
void ui_offline(const DeviceConfig *cfg, const NetStatus *st, uint32_t last_ok_ms);
char ui_offline_hit(int16_t x, int16_t y);
void ui_settings(const DeviceConfig *cfg, const char *status_line);
char ui_settings_hit(int16_t x, int16_t y);
void ui_render(const Snapshot *snap, uint8_t page, bool online, int8_t rssi, bool dark,
               const NotifyInfo *notification = nullptr, uint32_t remain_ms = 0);
void ui_overlay(const NotifyInfo *n, uint32_t remain_ms);
