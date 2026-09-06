#!/usr/bin/env python3
"""Factory partition written by the AuraGo web flasher."""

from __future__ import annotations

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class FactoryCfgTests(unittest.TestCase):
    def test_partition_table_has_cydcfg(self) -> None:
        table = (ROOT / "partitions" / "cyd.csv").read_text(encoding="utf-8")
        self.assertIn("cydcfg", table)
        self.assertIn("0x1F0000", table)
        self.assertIn("0x1000", table)
        ini = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertIn("partitions/cyd.csv", ini)

    def test_firmware_reads_factory_blob(self) -> None:
        header = (ROOT / "include" / "factory_cfg.h").read_text(encoding="utf-8")
        src = (ROOT / "src" / "factory_cfg.cpp").read_text(encoding="utf-8")
        store = (ROOT / "src" / "config_store.cpp").read_text(encoding="utf-8")
        self.assertIn('FACTORY_CFG_MAGIC "AGCY"', header)
        self.assertIn("FACTORY_CFG_OFFSET 0x1F0000", header)
        self.assertIn("esp_partition_find_first", src)
        self.assertIn('doc["token"]', src)
        self.assertIn('doc["url"]', src)
        self.assertIn("factory_cfg_apply", store)
        self.assertIn("config_save(cfg)", store)

    def test_protocol_documents_factory_blob(self) -> None:
        proto = (ROOT / "docs" / "protocol.md").read_text(encoding="utf-8")
        self.assertIn("AGCY", proto)
        self.assertIn("0x1F0000", proto)


if __name__ == "__main__":
    unittest.main()
