# AuraGo CYD wire protocol

Firmware in this repository speaks a compact JSON protocol so an ESP32-2432S028R
(Cheap Yellow Display) can show AuraGo status and agent notifications.

Base URL: `{aurago}/api/cyd`  
Auth: `Authorization: Bearer aura_…` with token scope `cyd`.  
The WebSocket upgrade may also pass `?token=aura_…` because some ESP32 WS clients
cannot set headers.

Payloads stay at or under ~1.5 KB. Unknown JSON fields are ignored.

## Snapshot — `GET /api/cyd/snapshot`

```json
{
  "ts": 1777650000,
  "agent": {
    "busy": false,
    "model": "grok-4",
    "personality": "default",
    "task": ""
  },
  "host": {
    "cpu_pct": 42.1,
    "mem_pct": 61.0,
    "disk_pct": 28.4,
    "uptime_s": 372012,
    "host_uptime_s": 1200000
  },
  "work": {
    "missions_running": 0,
    "missions_queued": 2,
    "notes_open": 3,
    "last_user_h": 0.2
  },
  "display": {
    "page": "status",
    "brightness": 180,
    "led": "green"
  },
  "notify": null
}
```

`notify` is either `null` or:

```json
{
  "id": "ntf_01HEXAMPLE",
  "title": "Backup failed",
  "body": "disk /data 98% — prune now",
  "priority": "critical",
  "ttl_s": 60
}
```

String limits (firmware truncates): `title` 32, `body` 96, `task` 40, `model` 23.

`priority`: `low` | `normal` | `high` | `critical`.  
`display.page`: `status` | `load`.  
`display.led`: `off` | `green` | `yellow` | `red` | `blue`.  
`display.brightness`: 0–255.

Polling fallback: every `poll_seconds` (default 5, min 2) if the WebSocket is down.

## WebSocket — `GET /api/cyd/ws`

Server → device:

```json
{ "type": "snapshot", "data": { } }
{ "type": "notify", "id": "ntf_01HEXAMPLE", "title": "Backup failed", "body": "…", "priority": "critical", "ttl_s": 60 }
{ "type": "clear", "id": "ntf_01HEXAMPLE" }
{ "type": "led", "color": "yellow" }
{ "type": "page", "page": "load" }
{ "type": "ping" }
```

Device → server: `{"type":"pong"}`, `{"type":"ack","id":"…","action":"dismiss"}`, `{"type":"heartbeat",…}`.

One overlay at a time. A new notification replaces a lower-or-equal priority.
Critical never yields to normal. TTL default 30 s, critical 60 s, max 300 s.

## Heartbeat — `POST /api/cyd/heartbeat`

```json
{
  "firmware": "0.1.0",
  "variant": "cyd",
  "rssi": -48,
  "width": 320,
  "height": 240
}
```

## Ack — `POST /api/cyd/ack`

```json
{ "id": "ntf_01HEXAMPLE", "action": "dismiss" }
```

## Errors

| HTTP | Meaning |
|---|---|
| 401 | missing or unknown token |
| 403 | token lacks scope `cyd` |
| 429 | more than 2 snapshot requests per second |

LAN only in v1. HTTPS is optional; firmware may skip TLS verification when
the provisioned URL starts with `https://` (documented as insecure).
