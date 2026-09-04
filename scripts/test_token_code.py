#!/usr/bin/env python3
"""Contract tests for the 9-character on-glass CYD token."""

from __future__ import annotations

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"


def normalize_token(raw: str) -> str:
    compact = re.sub(r"[\s-]+", "", raw or "")
    body = compact
    if compact.lower().startswith("aura_"):
        body = compact[5:]
    if len(body) == 9:
        return "aura_" + body.upper()
    if compact.lower().startswith("aura_"):
        return "aura_" + compact[5:]
    if compact:
        return "aura_" + compact
    return ""


class TokenCodeTests(unittest.TestCase):
    def test_groups_of_three_without_typing_prefix(self) -> None:
        self.assertEqual(normalize_token("k7m 2pq 9xh"), "aura_K7M2PQ9XH")
        self.assertEqual(normalize_token("aura_k7m-2pq-9xh"), "aura_K7M2PQ9XH")

    def test_legacy_hex_token_unchanged(self) -> None:
        legacy = "aura_0123456789abcdef0123456789abcdef"
        self.assertEqual(normalize_token(legacy), legacy)

    def test_firmware_has_on_glass_keypad_and_aura_prefix(self) -> None:
        provision = (ROOT / "src" / "provision.cpp").read_text(encoding="utf-8")
        ui_h = (ROOT / "include" / "ui.h").read_text(encoding="utf-8")
        ui_c = (ROOT / "src" / "ui.cpp").read_text(encoding="utf-8")
        store = (ROOT / "src" / "config_store.cpp").read_text(encoding="utf-8")
        self.assertIn("provision_enter_token", provision)
        self.assertIn("ui_token_entry", provision)
        self.assertIn("void ui_token_entry", ui_h)
        self.assertIn("aura_", ui_c)
        self.assertIn(ALPHABET, ui_c)
        self.assertIn("config_normalize_token", store)
        self.assertIn("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", store)


if __name__ == "__main__":
    unittest.main()
