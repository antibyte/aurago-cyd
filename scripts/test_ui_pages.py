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

    def test_alerts_mesh_protocol(self) -> None:
        self.assertIn("struct AlertsInfo", self.protocol_h)
        self.assertIn("struct MeshInfo", self.protocol_h)
        self.assertIn('root["alerts"]', self.protocol_c)
        self.assertIn('root["mesh"]', self.protocol_c)
        self.assertIn("alerts", self.proto)
        self.assertIn("mesh", self.proto)

    def test_firmware_version_bumped(self) -> None:
        self.assertIn('-DFIRMWARE_VERSION=\\"0.3.0\\"', self.ini)


if __name__ == "__main__":
    unittest.main()
