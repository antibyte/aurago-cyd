#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct DeviceConfig {
  char aurago_url[128];
  char token[80];
  char host[64];
  char path_prefix[32];
  uint16_t port;
  bool use_tls;
  bool demo;
  uint8_t poll_seconds;
  bool url_ok;
  bool dark_mode;
};

void config_load(DeviceConfig *cfg);
void config_save(const DeviceConfig *cfg);
void config_clear();
bool config_parse_url(DeviceConfig *cfg);
void config_format_url(DeviceConfig *cfg);
bool config_is_demo(const DeviceConfig *cfg);
void config_normalize_token(char *dst, size_t cap, const char *src);
bool config_token_ready(const char *token);
