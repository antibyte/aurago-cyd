#!/usr/bin/env python3
"""Glass dashboard: five pages, badges, carousel, alerts/mesh."""

from __future__ import annotations

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class UIPagesTests(unittest.TestCase):
    def setUp(self) -> None:
        self.ui_h = (ROOT / "include" / "ui.h").read_text(encoding="utf-8")
        self.ui_c = (ROOT / "src" / "ui.cpp").read_text(encoding="utf-8")
        self.main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
        self.ini = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.proto = (ROOT / "docs" / "protocol.md").read_text(encoding="utf-8")
        self.protocol_h = (ROOT / "include" / "protocol.h").read_text(encoding="utf-8")
        self.protocol_c = (ROOT / "src" / "protocol.cpp").read_text(encoding="utf-8")

    def test_five_dashboard_pages(self) -> None:
        self.assertIn("#define UI_PAGE_COUNT 5", self.ui_h)
        for name in ("Home", "Load", "Work", "Alerts", "Mesh"):
            self.assertIn(name, self.ui_c)
        self.assertIn("draw_alerts_page", self.ui_c)
        self.assertIn("draw_mesh_page", self.ui_c)
        self.assertIn("draw_badge", self.ui_c)

    def test_carousel_idle_rotate(self) -> None:
        self.assertIn("UI_IDLE_MS", self.ui_h)
        self.assertIn("UI_ROTATE_MS", self.main)
        self.assertIn("note_touch", self.main)
        self.assertIn("last_touch", self.main)
        self.assertIn("last_page_ms", self.main)
        self.assertIn("carousel_on", self.main)
        self.assertNotIn("last_rotate", self.main)
        self.assertIn("nowms - last_page_ms >= UI_ROTATE_MS", self.main)
        self.assertIn("nowms - last_touch >= UI_IDLE_MS", self.main)

    def test_snapshot_does_not_steal_page(self) -> None:
        start = self.main.find("case WsType::Snapshot:")
        end = self.main.find("case WsType::Notify:")
        self.assertGreater(start, 0)
        self.assertGreater(end, start)
        block = self.main[start:end]
        self.assertNotIn("page =", block)
        self.assertNotIn("ui_page_from_name", block)
        fetch = self.main[self.main.find("if (net_fetch_snapshot") :]
        self.assertNotIn("ui_page_from_name", fetch)

    def test_settings_holds_dashboard(self) -> None:
        self.assertIn("if (dirty && !settings_open)", self.main)
        open_fn = self.main[
            self.main.find("static void open_settings()") : self.main.find("static void settings_test()")
        ]
        self.assertIn("note_touch()", open_fn)
        settings_loop = self.main[self.main.find("if (settings_open)") : self.main.find("if (touch.tap || touch.swipe_left")]
        self.assertIn("note_touch()", settings_loop)

    def test_alerts_mesh_protocol(self) -> None:
        self.assertIn("struct AlertsInfo", self.protocol_h)
        self.assertIn("struct MeshInfo", self.protocol_h)
        self.assertIn('root["alerts"]', self.protocol_c)
        self.assertIn('root["mesh"]', self.protocol_c)
        self.assertIn("alerts", self.proto)
        self.assertIn("mesh", self.proto)

    def test_firmware_version_bumped(self) -> None:
        self.assertIn('-DFIRMWARE_VERSION=\\"0.3.13\\"', self.ini)


if __name__ == "__main__":
    unittest.main()
