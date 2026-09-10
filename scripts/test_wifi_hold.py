#!/usr/bin/env python3
"""STA must stay up: no modem sleep, auto-reconnect, WS heartbeat."""

from __future__ import annotations

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class WifiHoldTests(unittest.TestCase):
    def test_sleep_disabled_and_reconnect(self) -> None:
        prov = (ROOT / "src" / "provision.cpp").read_text(encoding="utf-8")
        net = (ROOT / "src" / "net_client.cpp").read_text(encoding="utf-8")
        hold = prov[prov.find("void wifi_apply_hold") : prov.find("static char g_ap_ssid")]
        self.assertIn("wifi_apply_hold", prov)
        self.assertIn("WIFI_PS_NONE", hold)
        self.assertIn("setAutoReconnect(true)", hold)
        self.assertNotIn("WiFi.mode(WIFI_STA)", hold)
        self.assertNotIn("esp_wifi_set_inactive_time", prov)
        self.assertIn("WiFi.reconnect()", net)
        self.assertNotIn("enableHeartbeat", net)
        self.assertIn("last_wifi_try = millis()", net)
        self.assertIn('set_error("wifi down")', net)
        self.assertIn("wifi_sta_from_nvs", prov)
        self.assertIn("wifi_use_eu_channels", prov)
        self.assertIn("WIFI_AUTH_WPA2_PSK", prov)
        self.assertIn("esp_wifi_connect()", prov)


if __name__ == "__main__":
    unittest.main()
