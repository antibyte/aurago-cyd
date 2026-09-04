#!/usr/bin/env python3
"""HTTP contract tests for the CYD mock server."""

from __future__ import annotations

import json
import pathlib
import sys
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import mock_server  # noqa: E402

EXAMPLE = json.loads((ROOT / "examples" / "snapshot.json").read_text(encoding="utf-8"))


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        mock_server.TOKEN = "aura_dev"
        mock_server.reset_state()
        cls.httpd = mock_server.serve("127.0.0.1", 0)
        cls.port = cls.httpd.server_address[1]
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = "http://127.0.0.1:%s" % cls.port

    @classmethod
    def tearDownClass(cls) -> None:
        cls.httpd.shutdown()
        cls.httpd.server_close()

    def request(self, method: str, path: str, body: dict | None = None, token: str | None = "aura_dev"):
        data = None if body is None else json.dumps(body).encode("utf-8")
        headers = {"Accept": "application/json"}
        if token:
            headers["Authorization"] = "Bearer %s" % token
        if data is not None:
            headers["Content-Type"] = "application/json"
        req = Request(self.base + path, data=data, headers=headers, method=method)
        with urlopen(req, timeout=3) as resp:
            raw = resp.read()
            return resp.status, json.loads(raw.decode("utf-8"))

    def test_example_snapshot_shape(self) -> None:
        for key in ("ts", "agent", "host", "work", "display", "notify"):
            self.assertIn(key, EXAMPLE)
        self.assertIn("busy", EXAMPLE["agent"])
        self.assertIn("cpu_pct", EXAMPLE["host"])
        self.assertIsNone(EXAMPLE["notify"])

    def test_snapshot_requires_token(self) -> None:
        with self.assertRaises(HTTPError) as ctx:
            self.request("GET", "/api/cyd/snapshot", token=None)
        self.assertEqual(ctx.exception.code, 401)

    def test_snapshot_ok(self) -> None:
        status, payload = self.request("GET", "/api/cyd/snapshot")
        self.assertEqual(status, 200)
        self.assertIn("agent", payload)
        self.assertIn("host", payload)
        self.assertIn("work", payload)
        self.assertIn("cpu_pct", payload["host"])
        self.assertIn("busy", payload["agent"])

    def test_heartbeat_and_notify_and_ack(self) -> None:
        status, _ = self.request(
            "POST",
            "/api/cyd/heartbeat",
            {"firmware": "0.1.0", "variant": "cyd", "rssi": -40, "width": 320, "height": 240},
        )
        self.assertEqual(status, 200)

        status, injected = self.request(
            "POST",
            "/api/cyd/test",
            {"id": "ntf_test", "title": "Backup failed", "body": "disk 98%", "priority": "critical", "ttl_s": 30},
        )
        self.assertEqual(status, 200)
        self.assertEqual(injected["notify"]["id"], "ntf_test")

        _, snap = self.request("GET", "/api/cyd/snapshot")
        self.assertIsNotNone(snap["notify"])
        self.assertEqual(snap["notify"]["title"], "Backup failed")
        self.assertEqual(snap["notify"]["priority"], "critical")
        self.assertLessEqual(len(snap["notify"]["title"]), 32)
        self.assertLessEqual(len(snap["notify"]["body"]), 96)

        status, _ = self.request("POST", "/api/cyd/ack", {"id": "ntf_test", "action": "dismiss"})
        self.assertEqual(status, 200)
        _, snap = self.request("GET", "/api/cyd/snapshot")
        self.assertIsNone(snap["notify"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
