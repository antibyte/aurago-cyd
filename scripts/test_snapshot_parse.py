#!/usr/bin/env python3
"""Firmware must read the full snapshot body, not the first TCP fragment."""

from __future__ import annotations

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class SnapshotParseTests(unittest.TestCase):
    def test_http_client_reads_full_payload(self) -> None:
        src = (ROOT / "src" / "net_client.cpp").read_text(encoding="utf-8")
        self.assertIn("getString()", src)
        self.assertNotIn("getStreamPtr()", src)

    def test_parse_error_is_specific(self) -> None:
        proto = (ROOT / "src" / "protocol.cpp").read_text(encoding="utf-8")
        net = (ROOT / "src" / "net_client.cpp").read_text(encoding="utf-8")
        self.assertIn("snapshot_parse_error", proto)
        self.assertIn("snapshot_parse_error", net)

    def test_uses_arduinojson7_object_const(self) -> None:
        proto = (ROOT / "src" / "protocol.cpp").read_text(encoding="utf-8")
        self.assertIn("JsonObjectConst", proto)
        self.assertNotIn("!root.is<JsonObject>()", proto)

    def test_parses_alerts_and_mesh_feeds(self) -> None:
        proto = (ROOT / "src" / "protocol.cpp").read_text(encoding="utf-8")
        self.assertIn("parse_feed_items", proto)
        self.assertIn('root["alerts"]', proto)
        self.assertIn('root["mesh"]', proto)


if __name__ == "__main__":
    unittest.main()
