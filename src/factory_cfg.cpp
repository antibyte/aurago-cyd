#include "factory_cfg.h"
#include "protocol.h"

#include <ArduinoJson.h>
#include <esp_partition.h>
#include <string.h>

bool factory_cfg_apply(DeviceConfig *cfg) {
  if (cfg == nullptr) {
    return false;
  }
  const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "cydcfg");
  if (part == nullptr) {
    return false;
  }
  uint8_t buf[FACTORY_CFG_SIZE];
  if (esp_partition_read(part, 0, buf, sizeof(buf)) != ESP_OK) {
    return false;
  }
  if (memcmp(buf, FACTORY_CFG_MAGIC, 4) != 0 || buf[4] != FACTORY_CFG_VERSION) {
    return false;
  }
  uint16_t n = static_cast<uint16_t>(buf[6] | (static_cast<uint16_t>(buf[7]) << 8));
  if (n == 0 || n >= 2000 || static_cast<size_t>(8 + n) > sizeof(buf)) {
    return false;
  }
  char json[2008];
  memcpy(json, buf + 8, n);
  json[n] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }
  const char *url = doc["url"] | "";
  const char *token = doc["token"] | "";
  if (url[0] == '\0' && token[0] == '\0') {
    return false;
  }
  if (url[0] != '\0') {
    copy_trunc(cfg->aurago_url, sizeof(cfg->aurago_url), url);
    config_parse_url(cfg);
    cfg->demo = config_is_demo(cfg);
  }
  if (token[0] != '\0') {
    config_normalize_token(cfg->token, sizeof(cfg->token), token);
  }
  return config_token_ready(cfg->token) || cfg->url_ok;
}
