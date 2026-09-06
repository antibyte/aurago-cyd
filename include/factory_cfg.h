#pragma once

#include "config_store.h"

#define FACTORY_CFG_MAGIC "AGCY"
#define FACTORY_CFG_VERSION 1
#define FACTORY_CFG_SIZE 4096
#define FACTORY_CFG_OFFSET 0x1F0000

// Reads the cydcfg flash partition written by the AuraGo web flasher.
// On success, copies URL and token into cfg (does not save NVS).
bool factory_cfg_apply(DeviceConfig *cfg);
