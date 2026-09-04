# aurago-cyd

Firmware for the [ESP32 Cheap Yellow Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
(ESP32-2432S028R, 320×240 resistive TFT). It is a LAN mini-dashboard for
[AuraGo](https://github.com/antibyte/AuraGo): agent idle/busy, host load, missions,
and agent-pushed notification overlays.

```
┌────────────────────────────────┐
│ AURAGO   status   21:04   ▮▮▮▮ │
│ idle              grok-4       │
│ NOW  waiting                   │
│ LAST 12m ago                   │
│ missions 0 run / 2 queued      │
│ notes 3 open              1/2  │
└────────────────────────────────┘
```

## Hardware

| Board | PlatformIO env | Notes |
|---|---|---|
| Single micro-USB CYD | `cyd` (default) | ILI9341_2 |
| Dual USB (micro + USB-C) CYD2USB | `cyd2usb` | ST7789, BGR |

The USB-C port on CYD2USB often lacks CC resistors. Use USB-A to USB-C, not C-to-C.

Install the CH340 driver if the board is not enumerated.

## Flash

Requires [PlatformIO](https://platformio.org/):

```bash
pio run -e cyd -t upload
pio device monitor
```

CYD2USB:

```bash
pio run -e cyd2usb -t upload
```

If upload fails, keep `upload_speed = 115200` (already set) and hold **BOOT** while tapping **RESET**.

## First boot

1. The display opens a captive portal AP named `agocyd-XXXX` and shows a
   Wi-Fi QR code. Scan it to join (open network, no password). Then open
   `192.168.4.1` if the portal does not appear by itself.
2. Join it and set:
   - Wi-Fi SSID / password
   - **AuraGo URL** — `http://192.168.x.x:8088` or `demo`
3. On the glass, type the 9-character display code from AuraGo Config
   (**Cheap Yellow Display**). It is shown as `XXX XXX XXX`. `aura_` is
   already filled in; do not type the prefix.
4. After connect, page 1 is status, page 2 is CPU/RAM/disk. Swipe to switch.
5. Tap an overlay to dismiss it. RGB LED: green idle, yellow busy, red error/critical, blue connecting.
6. Hold **BOOT** for 5 seconds at power-on to wipe config and reopen the portal.

`demo` as the URL runs an offline animated dashboard (no AuraGo required).

AuraGo must listen on a LAN address, not only `127.0.0.1`. Default AuraGo bind is loopback;
set `server.host` (or use Tailscale) so the CYD can reach it.

## Protocol

See [docs/protocol.md](docs/protocol.md). Short version:

| Method | Path | Role |
|---|---|---|
| GET | `/api/cyd/snapshot` | compact dashboard JSON |
| GET | `/api/cyd/ws` | live snapshot + notify |
| POST | `/api/cyd/heartbeat` | firmware / RSSI |
| POST | `/api/cyd/ack` | dismiss overlay |

Auth: `Authorization: Bearer aura_…`. WebSocket may use `?token=`.

Mock host (no AuraGo):

```bash
python scripts/mock_server.py --port 8088 --token aura_dev
python scripts/test_protocol.py
```

Point the CYD URL at `http://<pc-lan-ip>:8088` and token `aura_dev`. Inject an overlay:

```bash
curl -H "Authorization: Bearer aura_dev" -H "Content-Type: application/json" ^
  -d "{\"title\":\"Backup failed\",\"body\":\"disk 98%\",\"priority\":\"critical\"}" ^
  http://127.0.0.1:8088/api/cyd/test
```

## Layout

```
include/     public headers
src/         firmware
docs/        wire protocol
examples/    golden snapshot JSON
scripts/     mock server + protocol tests
```

Pins follow the upstream CYD map: TFT on HSPI (12/13/14/15/2/21), touch XPT2046 on
25/32/39/33/36, RGB LED 4/16/17 (active low), LDR 34.

## Agent (AuraGo)

The matching AuraGo integration publishes `/api/cyd/*`, adds `send_notification`
channel `cyd`, and the `cyd_display` tool. This repository is the device side.

## License

MIT. Display pin documentation based on the CYD community project
(witnessmenow/ESP32-Cheap-Yellow-Display, MIT).
