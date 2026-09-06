#!/usr/bin/env python3
"""Glass dashboard: four pages, gauges, sparkline, Orbitron, pager UX."""

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

    def test_four_dashboard_pages(self) -> None:
        self.assertIn("#define UI_PAGE_COUNT 4", self.ui_h)
        for name in ("HOME", "LOAD", "WORK", "HOST"):
            self.assertIn(name, self.ui_c)
        self.assertIn("ui_page_from_name", self.ui_h)
        self.assertIn("ui_page_hit", self.ui_h)
        self.assertIn("ui_note_metrics", self.ui_h)
        self.assertIn("ui_set_link_info", self.ui_h)

    def test_fancy_fonts_and_diagrams(self) -> None:
        self.assertIn("-DLOAD_FONT6", self.ini)
        self.assertIn("-DLOAD_FONT7", self.ini)
        self.assertIn("-DLOAD_GFXFF", self.ini)
        self.assertIn("Orbitron_Light_32", self.ui_c)
        self.assertIn("Orbitron_Light_24", self.ui_c)
        self.assertIn("drawArc", self.ui_c)
        self.assertIn("ui_note_metrics", self.ui_c)
        self.assertIn("draw_spark", self.ui_c)

    def test_pager_ux_and_wrap(self) -> None:
        self.assertIn("ui_page_hit", self.main)
        self.assertIn("UI_PAGE_COUNT", self.main)
        self.assertIn("ui_note_metrics", self.main)
        self.assertIn("ui_set_link_info", self.main)
        self.assertIn("ui_page_from_name", self.main)
        self.assertNotIn("page == 0 ? 1 : 0", self.main)
        self.assertNotIn("%u/2", self.ui_c)

    def test_protocol_lists_new_pages(self) -> None:
        self.assertIn("work", self.proto)
        self.assertIn("host", self.proto)

    def test_firmware_version_bumped(self) -> None:
        self.assertIn('-DFIRMWARE_VERSION=\\"0.2.1\\"', self.ini)


if __name__ == "__main__":
    unittest.main()
