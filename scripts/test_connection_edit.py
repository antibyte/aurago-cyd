#!/usr/bin/env python3
"""Contract tests for on-glass AuraGo URL editing."""

from __future__ import annotations

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ConnectionEditTests(unittest.TestCase):
    def test_firmware_can_edit_url_from_connection_screen(self) -> None:
        ui_h = (ROOT / "include" / "ui.h").read_text(encoding="utf-8")
        ui_c = (ROOT / "src" / "ui.cpp").read_text(encoding="utf-8")
        main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
        self.assertIn("void ui_settings", ui_h)
        self.assertIn("ui_offline_hit", ui_h)
        self.assertIn("ui_settings_hit", ui_h)
        self.assertIn("Edit", ui_c)
        self.assertIn("Test", ui_c)
        self.assertIn("HTTPS", ui_c)
        self.assertIn("ui_offline_hit", main)
        self.assertIn("ui_settings", main)

    def test_bare_ip_does_not_force_port_80(self) -> None:
        store = (ROOT / "src" / "config_store.cpp").read_text(encoding="utf-8")
        self.assertIn("config_format_url", store)
        self.assertNotIn("cfg->port = 80;", store)

    def test_net_can_reconfigure_without_reboot(self) -> None:
        net_h = (ROOT / "include" / "net_client.h").read_text(encoding="utf-8")
        self.assertIn("net_reconfigure", net_h)


if __name__ == "__main__":
    unittest.main()
