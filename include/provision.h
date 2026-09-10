#pragma once

#include "config_store.h"

bool provision_connect(DeviceConfig *cfg, bool force_portal);
bool provision_enter_token(DeviceConfig *cfg, bool force = false);
bool provision_edit_url(DeviceConfig *cfg);
void wifi_apply_hold();
