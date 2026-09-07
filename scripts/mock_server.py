#!/usr/bin/env python3
"""Local AuraGo CYD protocol mock. Polling only (no WebSocket)."""

from __future__ import annotations

import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import parse_qs, urlparse

TOKEN = "aura_dev"
STATE: dict[str, Any] = {}
LOCK = threading.Lock()


def default_snapshot() -> dict[str, Any]:
    now = int(time.time())
    busy = (now // 8) % 2 == 1
    return {
        "ts": now,
        "agent": {
            "busy": busy,
            "model": "mock-llm",
            "personality": "default",
            "task": "compiling firmware" if busy else "",
        },
        "host": {
            "cpu_pct": 28.0 + (now % 40),
            "mem_pct": 55.0,
            "disk_pct": 24.0,
            "uptime_s": now % 100000,
            "host_uptime_s": 86400 + (now % 100000),
        },
        "work": {
            "missions_running": 1 if busy else 0,
            "missions_queued": 2,
            "notes_open": 3,
            "last_user_h": 0.2,
        },
        "display": {
            "page": "status",
            "brightness": 180,
            "led": "yellow" if busy else "green",
        },
        "notify": None,
        "alerts": {"count": 0, "items": []},
        "mesh": {"unread": 0, "items": []},
    }


def reset_state() -> None:
    with LOCK:
        STATE.clear()
        STATE["snapshot"] = default_snapshot()
        STATE["heartbeats"] = []
        STATE["acks"] = []


def current_snapshot() -> dict[str, Any]:
    with LOCK:
        snap = json.loads(json.dumps(STATE["snapshot"]))
        notify = snap.get("notify")
        if notify and notify.get("expires_at"):
            if time.time() > float(notify["expires_at"]):
                snap["notify"] = None
                STATE["snapshot"]["notify"] = None
            else:
                notify = dict(notify)
                notify.pop("expires_at", None)
                snap["notify"] = notify
        return snap


def authorized(handler: BaseHTTPRequestHandler) -> bool:
    if TOKEN == "":
        return True
    header = handler.headers.get("Authorization", "")
    query = parse_qs(urlparse(handler.path).query)
    token = ""
    if header.lower().startswith("bearer "):
        token = header[7:].strip()
    if not token:
        token = (query.get("token") or [""])[0]
    return token == TOKEN


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args: Any) -> None:
        print("[%s] %s" % (self.log_date_time_string(), fmt % args))

    def _json(self, code: int, payload: Any) -> None:
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length") or "0")
        if length <= 0:
            return {}
        body = self.rfile.read(length)
        if not body:
            return {}
        return json.loads(body.decode("utf-8"))

    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path in ("/", "/health"):
            self._json(200, {"status": "ok", "service": "aurago-cyd-mock"})
            return
        if not authorized(self):
            self._json(401, {"error": "unauthorized"})
            return
        if path == "/api/cyd/snapshot":
            self._json(200, current_snapshot())
            return
        if path == "/api/cyd/status":
            with LOCK:
                self._json(
                    200,
                    {
                        "heartbeats": len(STATE["heartbeats"]),
                        "acks": list(STATE["acks"]),
                    },
                )
            return
        self._json(404, {"error": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if not authorized(self):
            self._json(401, {"error": "unauthorized"})
            return
        payload = self._read_json()
        if path == "/api/cyd/heartbeat":
            with LOCK:
                STATE["heartbeats"].append(payload)
            self._json(200, {"status": "ok"})
            return
        if path == "/api/cyd/ack":
            with LOCK:
                STATE["acks"].append(payload.get("id"))
                notify = STATE["snapshot"].get("notify") or {}
                if payload.get("id") and notify.get("id") == payload.get("id"):
                    STATE["snapshot"]["notify"] = None
            self._json(200, {"status": "ok"})
            return
        if path in ("/api/cyd/test", "/api/cyd/notify"):
            ttl = int(payload.get("ttl_s") or 30)
            notify = {
                "id": payload.get("id") or "ntf_mock",
                "title": payload.get("title") or "AuraGo",
                "body": payload.get("message") or payload.get("body") or "test notification",
                "priority": payload.get("priority") or payload.get("tag") or "normal",
                "ttl_s": ttl,
                "expires_at": time.time() + ttl,
            }
            with LOCK:
                STATE["snapshot"]["notify"] = notify
            shown = dict(notify)
            shown.pop("expires_at", None)
            self._json(200, {"status": "ok", "notify": shown})
            return
        self._json(404, {"error": "not found"})


def serve(host: str, port: int) -> ThreadingHTTPServer:
    reset_state()
    httpd = ThreadingHTTPServer((host, port), Handler)
    return httpd


def main() -> None:
    parser = argparse.ArgumentParser(description="AuraGo CYD protocol mock")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8088)
    parser.add_argument("--token", default="aura_dev")
    args = parser.parse_args()
    global TOKEN
    TOKEN = args.token
    httpd = serve(args.host, args.port)
    print("aurago-cyd mock on http://%s:%s  token=%s" % (args.host, args.port, TOKEN or "(none)"))
    print("  GET  /api/cyd/snapshot")
    print("  POST /api/cyd/heartbeat")
    print("  POST /api/cyd/ack")
    print("  POST /api/cyd/test   {title, body, priority, ttl_s}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstop")
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
