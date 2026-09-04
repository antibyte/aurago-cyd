#!/usr/bin/env python3
"""Contract tests for the unpaired CYD Wi-Fi join QR."""

from __future__ import annotations

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def ap_ssid(mac: bytes) -> str:
    if len(mac) != 6:
        raise ValueError("mac must be 6 bytes")
    return "agocyd-%02X%02X" % (mac[4], mac[5])


def wifi_join_qr(ssid: str) -> str:
    if not ssid:
        raise ValueError("ssid required")
    return "WIFI:T:nopass;S:%s;;" % ssid


class PairingQRContractTests(unittest.TestCase):
    def test_ssid_uses_last_two_mac_bytes(self) -> None:
        self.assertEqual(ap_ssid(bytes.fromhex("d0ef7658e860")), "agocyd-E860")
        self.assertEqual(ap_ssid(bytes.fromhex("aabbccddeeff")), "agocyd-EEFF")

    def test_wifi_qr_is_open_network_join_payload(self) -> None:
        self.assertEqual(
            wifi_join_qr("agocyd-E860"),
            "WIFI:T:nopass;S:agocyd-E860;;",
        )

    def test_firmware_ssid_format_matches_contract(self) -> None:
        src = (ROOT / "src" / "provision.cpp").read_text(encoding="utf-8")
        self.assertRegex(src, r'agocyd-%02X%02X')

    def test_firmware_builds_open_wifi_qr_and_shows_it_in_portal(self) -> None:
        provision = (ROOT / "src" / "provision.cpp").read_text(encoding="utf-8")
        ui_h = (ROOT / "include" / "ui.h").read_text(encoding="utf-8")
        ui_c = (ROOT / "src" / "ui.cpp").read_text(encoding="utf-8")
        self.assertIn("WIFI:T:nopass;S:", provision)
        self.assertIn("setAPCallback", provision)
        self.assertIn("ui_pairing", provision)
        self.assertIn("void ui_pairing", ui_h)
        self.assertIn("void ui_pairing", ui_c)
        self.assertIn("qrcode_initText", ui_c)
        self.assertRegex(ui_c, re.compile(r"192\.168\.4\.1|softAPIP"))


if __name__ == "__main__":
    unittest.main()
