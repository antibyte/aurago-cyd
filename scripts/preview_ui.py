#!/usr/bin/env python3
"""Compile the real ui.cpp on the host, assert bounds/hit targets, render previews.

Run after `python -m platformio run -e cyd`. Requires C++17 and Pillow.
Outputs .pio/ui-preview/*.png plus a contact sheet, without connecting hardware.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.pio' / 'ui-preview'
LIB = ROOT / '.pio' / 'libdeps' / 'cyd'

CHECK = r'''
bool config_is_demo(const DeviceConfig *c) { return c->demo; }
void copy_trunc(char *dst, size_t cap, const char *src) { snprintf(dst, cap, "%s", src); }
int notify_rank(const char *p) { return !strcmp(p, "critical") ? 3 : !strcmp(p, "warning") ? 2 : 1; }
int main() {
  ui_begin();
  for (int x = 0; x < SCREEN_W; x++) {
    assert(ui_page_hit(x, 203) == 0);
    assert(ui_page_hit(x, 204) == '0' + x / 64);
    assert(ui_page_hit(x, 239) == '0' + x / 64);
  }
  for (int x : {-1, 320}) {
    assert(!ui_page_hit(x, 220)); assert(!ui_header_hit(x, 16));
    assert(!ui_settings_hit(x, 218)); assert(!ui_token_key_at(x, 120));
  }
  assert(!ui_page_hit(100, 240));
  assert(ui_header_hit(183, 16) == 'a' && ui_header_hit(210, 16) == 'm' && ui_header_hit(250, 16) == 'c');
  assert(ui_settings_hit(228, 76) == 'd' && ui_settings_hit(80, 190) == 's');
  assert(ui_offline_hit(160, 184) == 'e');
  for (int i = 0; i < 32; i++) assert(ui_token_key_at(i % 8 * 40 + 20, 100 + i / 8 * 26 + 12) == kTokenKeys[i]);
  for (int width : {16, 32, 96, 276}) {
    draw_fit("A deliberately long string / 1234567890", 8, 50, width, 2, COL_TEXT, COL_BG);
    assert(tft.last_width <= width);
  }
  draw_fit("Gr\xC3\xBC\xC3\x9F" "e\n", 8, 50, 276, 2, COL_TEXT, COL_BG);
  assert(tft.last_text == "Gr??e ");
  Snapshot s{};
  copy_trunc(s.agent.model, sizeof(s.agent.model), "qwen3.5-35b-a3b");
  s.host.cpu_pct = 42; s.host.mem_pct = 63; s.host.disk_pct = 27;
  s.host.host_uptime_s = 93182; s.work.missions_running = 1; s.work.missions_queued = 3;
  s.work.notes_open = 12; s.work.last_user_h = 0.2f;
  ui_set_link_info("192.168.1.42");
  for (int i = 0; i < 65; i++) ui_note_metrics(34 + 21 * sin(i * .63), 61 + 8 * cos(i * .35), 27);
  s.alerts.count = s.alerts.n = 3;
  copy_trunc(s.alerts.items[0].title, sizeof(s.alerts.items[0].title), "Backup needs attention");
  copy_trunc(s.alerts.items[0].sev, sizeof(s.alerts.items[0].sev), "warning");
  copy_trunc(s.alerts.items[1].title, sizeof(s.alerts.items[1].title), "Provider unavailable");
  copy_trunc(s.alerts.items[1].sev, sizeof(s.alerts.items[1].sev), "error");
  copy_trunc(s.alerts.items[2].title, sizeof(s.alerts.items[2].title), "Workspace synchronized");
  copy_trunc(s.alerts.items[2].sev, sizeof(s.alerts.items[2].sev), "info");
  s.mesh.unread = 2; s.mesh.n = 3;
  for (int i = 0; i < 3; i++) {
    copy_trunc(s.mesh.items[i].title, sizeof(s.mesh.items[i].title), i == 0 ? "Workshop / Anna" : i == 1 ? "Garden / Relay" : "Untrusted sender");
    copy_trunc(s.mesh.items[i].body, sizeof(s.mesh.items[i].body), i == 0 ? "All sensors back online" : "Evening check complete");
    s.mesh.items[i].age_s = 120 + i * 3800; s.mesh.items[i].locked = i == 2;
  }
  DeviceConfig cfg{}; cfg.port = 8443; cfg.use_tls = true;
  copy_trunc(cfg.host, sizeof(cfg.host), "192.168.1.20");
  NotifyInfo n{}; n.active = true;
  copy_trunc(n.title, sizeof(n.title), "Backup needs attention");
  copy_trunc(n.body, sizeof(n.body), "The nightly backup could not finish. Check the storage connection and retry from AuraGo.");
  copy_trunc(n.priority, sizeof(n.priority), "warning");
  for (bool dark : {true, false}) {
    std::string prefix = dark ? "dark-" : "light-";
    cfg.dark_mode = dark;
    for (int page = 0; page < 5; page++) {
      ui_render(&s, page, true, -61, dark); tft.save(prefix + page_title(page));
      auto buffered = std::vector<uint16_t>(tft.pixels, tft.pixels + 320 * 240);
      band.ready = false; ui_render(&s, page, true, -61, dark); band.ready = true;
      assert(std::equal(buffered.begin(), buffered.end(), tft.pixels));
    }
    s.agent.busy = true; copy_trunc(s.agent.task, sizeof(s.agent.task), "Indexing the project workspace");
    ui_render(&s, 0, true, -61, dark); tft.save(prefix + "Busy");
    ui_render(&s, 0, true, -61, dark, &n, 17000); tft.save(prefix + "Notification");
    auto composed = std::vector<uint16_t>(tft.pixels, tft.pixels + 320 * 240);
    band.ready = false; ui_render(&s, 0, true, -61, dark, &n, 17000); band.ready = true;
    assert(std::equal(composed.begin(), composed.end(), tft.pixels));
    ui_settings(&cfg, ""); tft.save(prefix + "Settings");
    NetStatus st{}; copy_trunc(st.error, sizeof(st.error), "Host not reachable. Retrying...");
    ui_offline(&cfg, &st, 90000); tft.save(prefix + "Offline");
    ui_pairing("agocyd-E860", "WIFI:T:nopass;S:agocyd-E860;;", "192.168.4.1"); tft.save(prefix + "Pairing");
    ui_token_entry("aura_", "K7M2P"); tft.save(prefix + "Token");
    ui_splash("Connecting to your workspace", "192.168.1.42"); tft.save(prefix + "Splash");
    Snapshot empty{}; ui_render(&empty, 3, true, -61, dark); tft.save(prefix + "Clear");
    ui_render(&empty, 4, true, -61, dark); tft.save(prefix + "Inbox");
    ui_render(nullptr, 0, false, -90, dark);
    // Exercise all bounded fields at their protocol capacity and large counters.
    Snapshot full = s;
    memset(full.agent.model, 'W', sizeof(full.agent.model) - 1);
    memset(full.agent.task, 'W', sizeof(full.agent.task) - 1);
    full.work.missions_running = full.work.missions_queued = full.work.notes_open = 2147483647;
    full.host.cpu_pct = 100; full.host.mem_pct = 0; full.host.disk_pct = 99;
    for (int p = 0; p < 5; p++) ui_render(&full, p, true, -40, dark);
    memset(n.title, 'W', sizeof(n.title) - 1); memset(n.body, 'W', sizeof(n.body) - 1);
    ui_overlay(&n, 0); tft.save(prefix + "Long-text");
    copy_trunc(n.body, sizeof(n.body), "\n\nMulti-line\nmessage with explicit breaks"); ui_overlay(&n, 0);
    memset(cfg.host, 'W', sizeof(cfg.host) - 1); ui_settings(&cfg, "Connection failed: host is unreachable");
    s.agent.busy = false; s.agent.task[0] = 0;
    copy_trunc(cfg.host, sizeof(cfg.host), "192.168.1.20");
    copy_trunc(n.title, sizeof(n.title), "Backup needs attention");
    copy_trunc(n.body, sizeof(n.body), "The nightly backup could not finish. Check the storage connection and retry from AuraGo.");
  }
}
'''

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    src = (ROOT / 'src/ui.cpp').read_text(encoding='utf-8')
    for inc in ('#include "hardware.h"', '#include <Arduino.h>', '#include <strings.h>'):
        src = src.replace(inc, '')
    # MSVC has no C99 VLA; the QR v3 buffer needs only 107 bytes.
    src = src.replace('qrcodeData[qrcode_getBufferSize(3)]', 'qrcodeData[512]')
    src = src.replace('localtime_r(&now, &t)', 'preview_localtime(&now, &t)')
    cpp = OUT / 'preview.cpp'
    cpp.write_text('#include "ui_preview.h"\n' + src + CHECK, encoding='utf-8')
    includes = [ROOT / 'scripts', ROOT / 'include', LIB / 'TFT_eSPI', LIB / 'QRCode/src']
    if os.name == 'nt':
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        vs = subprocess.check_output([str(vswhere), '-latest', '-property', 'installationPath'], text=True).strip()
        # Keep the real QR encoder; translate its C99 stack arrays for MSVC only.
        qr = (LIB / 'QRCode/src/qrcode.c').read_text()
        arrays = re.findall(r'uint8_t (\w+)\[([^\];]+)\];', qr)
        for name, size in arrays:
            qr = qr.replace(f'uint8_t {name}[{size}];', f'uint8_t *{name} = (uint8_t*)_alloca({size});')
            qr = qr.replace(f'sizeof({name})', f'({size})')
        qr = qr.replace('static int max(', '#undef max\nstatic int max(')
        (OUT / 'qrcode.c').write_text('#include <malloc.h>\n' + qr)
        command = ['cl', '/nologo', '/EHsc', '/std:c++17', '/wd4068', '/D_CRT_SECURE_NO_WARNINGS']
        command += ['/I' + str(p) for p in includes]
        command += [str(cpp), str(OUT / 'qrcode.c'), '/Fe:' + str(OUT / 'preview.exe')]
        build = OUT / 'build.cmd'
        build.write_text('@echo off\ncall "' + vs + '\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n' + subprocess.list2cmdline(command) + '\n')
        subprocess.run(['cmd', '/c', str(build)], cwd=OUT, check=True)
        exe = OUT / 'preview.exe'
    else:
        exe = OUT / 'preview'
        subprocess.run([shutil.which('c++') or 'g++', '-std=c++17', *['-I' + str(p) for p in includes], str(cpp), str(LIB / 'QRCode/src/qrcode.c'), '-o', str(exe)], cwd=OUT, check=True)
    subprocess.run([str(exe)], cwd=OUT, check=True)
    from PIL import Image, ImageDraw
    for ppm in OUT.glob('*.ppm'):
        with Image.open(ppm) as im:
            im.save(ppm.with_suffix('.png'))
    names = ['Home', 'Load', 'Work', 'Alerts', 'Mesh', 'Settings', 'Notification', 'Pairing']
    sheet = Image.new('RGB', (4 * 344 + 24, 4 * 280 + 24), '#10191e')
    draw = ImageDraw.Draw(sheet)
    for i, (theme, name) in enumerate((t, n) for t in ('dark', 'light') for n in names):
        x, y = 24 + i % 4 * 344, 24 + i // 4 * 280
        draw.text((x, y), f'{theme.upper()} / {name}', fill='#a4b8bc')
        with Image.open(OUT / f'{theme}-{name}.png') as im:
            sheet.paste(im, (x, y + 18))
    sheet.save(OUT / 'overview.png')
    print(f'UI bounds, navigation, keypad and text checks passed. Preview: {OUT / "overview.png"}')

if __name__ == '__main__':
    main()
