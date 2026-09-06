#!/usr/bin/env python3
"""Config button and dark mode (default on) on the CYD glass."""

from __future__ import annotations

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ThemeTests(unittest.TestCase):
    def test_dark_mode_defaults_on_and_is_persisted(self) -> None:
        store_h = (ROOT / "include" / "config_store.h").read_text(encoding="utf-8")
        store_c = (ROOT / "src" / "config_store.cpp").read_text(encoding="utf-8")
        self.assertIn("dark_mode", store_h)
        self.assertIn('getBool("dark", true)', store_c)
        self.assertIn('putBool("dark"', store_c)

    def test_settings_has_dark_toggle_and_header_config_button(self) -> None:
        ui_h = (ROOT / "include" / "ui.h").read_text(encoding="utf-8")
        ui_c = (ROOT / "src" / "ui.cpp").read_text(encoding="utf-8")
        main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
        self.assertIn("ui_set_dark", ui_h)
        self.assertIn("ui_header_hit", ui_h)
        self.assertIn("Dark", ui_c)
        self.assertIn("CFG", ui_c)
        self.assertIn("ui_set_dark", main)
        self.assertIn("ui_header_hit", main)
        self.assertIn("ui_render(&snap, page, online, hardware_wifi_rssi(), cfg.dark_mode)", main)


if __name__ == "__main__":
    unittest.main()
