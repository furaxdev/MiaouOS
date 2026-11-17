#!/usr/bin/env python3
"""Boot MiaouOS in QEMU, drive the desktop through QMP (real PS/2 mouse + keyboard events) and capture
real PNG screenshots of the guest display. Nothing is drawn or faked by this script: every image is a
QMP `screendump` of the emulated VGA adapter, and every claim is cross-checked against serial markers."""
import argparse, hashlib, json, os, socket, struct, subprocess, sys, time, zlib
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('--root', required=True)
ap.add_argument('--kernel'); ap.add_argument('--initrd')
ap.add_argument('--res', default='')            # e.g. 1024x768 (kernel parameter video=Virtual-1:WxH)
ap.add_argument('--theme', default='dark')
ap.add_argument('--tag', default='')            # screenshot filename prefix
ap.add_argument('--quick', action='store_true') # boot + one screenshot only
ap.add_argument('--iso', default='')              # boot this ISO (BIOS/GRUB) instead of kernel+initrd
ap.add_argument('--serial-log', default='')
ap.add_argument('--kbd', default='us')          # guest keyboard layout (miaou.kbd=)
ap.add_argument('--scenario', default='full', choices=['full', 'fr', 'res', 'iso', 'desk', 'tools', 'apps2', 'session', 'web'])
args = ap.parse_args()
root = Path(args.root); build = root / 'build'; shots = build / 'screenshots'
shots.mkdir(parents=True, exist_ok=True)
qmp_path = build / 'qmp.sock'; serial = Path(args.serial_log) if args.serial_log else build / 'serial.log'
kernel = args.kernel or str(build / 'miaouos-kernel-bzImage')
initrd = args.initrd or str(build / 'miaouos-initramfs.cpio.gz')
for p in (qmp_path, serial):
    try: p.unlink()
    except FileNotFoundError: pass
append = 'console=tty0 console=ttyS0,115200n8 rdinit=/init panic=5 quiet loglevel=3 vt.global_cursor_default=0 miaou.kbd=%s miaou.theme=%s' % (args.kbd, args.theme)
if args.scenario in ('fr', 'res', 'desk', 'tools', 'apps2', 'web') or args.quick: append += ' miaou.autologin=miaou'
if args.res: append += ' video=Virtual-1:' + args.res
boot = ['-machine', 'pc', '-cdrom', args.iso, '-boot', 'd'] if args.iso else ['-machine', 'q35', '-kernel', kernel, '-initrd', initrd, '-append', append]
cmd = ['qemu-system-x86_64', *boot, '-m', '768M', '-smp', '2', '-accel', 'kvm', '-accel', 'tcg',
       '-display', 'none', '-vga', 'std', '-serial', f'file:{serial}',
       '-netdev', 'user,id=n0', '-device', 'e1000,netdev=n0', '-device', 'virtio-tablet-pci',
       '-qmp', f'unix:{qmp_path},server=on,wait=off', '-no-reboot', '-no-shutdown']
proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

def log_text():
    try: return serial.read_text(errors='replace')
    except FileNotFoundError: return ''

def fail(msg):
    print('FAIL:', msg, file=sys.stderr); (serial.parent / (serial.stem + '-final.log')).write_text(log_text()) if args.serial_log else (build / 'qemu-serial.log').write_text(log_text()); cleanup(); sys.exit(1)

def cleanup():
    if proc.poll() is None:
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait(timeout=5)

# ---------------------------------------------------------------- QMP
sock = None; deadline = time.time() + 60
while time.time() < deadline and sock is None:
    if proc.poll() is not None: fail('QEMU exited early: ' + proc.stderr.read().decode(errors='replace'))
    if qmp_path.exists():
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(str(qmp_path)); sock = s
        except OSError: time.sleep(0.2)
    else: time.sleep(0.2)
if sock is None: fail('QMP socket did not appear')
stream = sock.makefile('rwb', buffering=0); stream.readline()
def qmp(execute, **arguments):
    stream.write((json.dumps({'execute': execute, 'arguments': arguments}) + '\n').encode())
    while True:
        line = stream.readline()
        if not line: fail('QMP closed')
        msg = json.loads(line)
        if 'return' in msg or 'error' in msg: return msg
stream.write(b'{"execute":"qmp_capabilities"}\n'); stream.readline()

def png_from_ppm(path):
    data = path.read_bytes(); assert data[:2] == b'P6'
    parts = data.split(None, 4); w, h = int(parts[1]), int(parts[2]); pix = data[len(data) - w * h * 3:]
    raw = b''.join(b'\x00' + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d): c = struct.pack('>I', len(d)) + t + d; return c + struct.pack('>I', zlib.crc32(t + d))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

taken = []
def shot(name):
    path = shots / f'{args.tag}{name}.png'
    if path.exists(): path.unlink()
    r = qmp('screendump', filename=str(path), format='png')
    if 'error' in r:                                     # older QEMU: PPM only
        r = qmp('screendump', filename=str(path))
        if 'error' in r: fail('screendump failed: %s' % r)
        png_from_ppm(path)
    if path.read_bytes()[:8] != b'\x89PNG\r\n\x1a\n': fail(f'{path} is not a PNG')
    taken.append(path); print('screenshot', path.name, path.stat().st_size, 'bytes')

# ---------------------------------------------------------------- input helpers
def key(*names, hold=0.05):
    qmp('send-key', keys=[{'type': 'qcode', 'data': n} for n in names], **{'hold-time': int(hold * 1000)}); time.sleep(0.25)
CHARS = {':': None, ' ': 'spc', '-': 'minus', '=': 'equal', '/': 'slash', '.': 'dot', ',': 'comma', ';': 'semicolon', "'": 'apostrophe', '\n': 'ret'}
SHIFTED = {'*': '8', '(': '9', ')': '0', '+': 'equal', '_': 'minus', '!': '1', '?': 'slash', '@': '2', '#': '3', '%': '5'}
def typ(text):
    for c in text:
        if c in SHIFTED: key('shift', SHIFTED[c])
        elif c == ':': key('shift', 'semicolon')
        elif c in CHARS: key(CHARS[c])
        elif c.isupper(): key('shift', c.lower())
        elif c.isalnum(): key(c)
        else: raise ValueError(c)
def rel(dx, dy):
    """Relative pointer motion. QEMU emits one PS/2 packet (<=127 per axis) per QMP call and keeps the
    remainder in an accumulator, so large moves are sent as many small calls (otherwise they pile up)."""
    steps = max(1, -(-max(abs(dx), abs(dy)) // 120))
    sx = sy = 0
    for i in range(1, steps + 1):
        tx, ty = dx * i // steps, dy * i // steps
        mx, my = tx - sx, ty - sy; sx, sy = tx, ty
        ev = []
        if mx: ev.append({'type': 'rel', 'data': {'axis': 'x', 'value': mx}})
        if my: ev.append({'type': 'rel', 'data': {'axis': 'y', 'value': my}})
        if ev: qmp('input-send-event', events=ev); time.sleep(0.012)
    time.sleep(0.3)
def goto(x, y):
    """Absolute pointer positioning through the virtio tablet (MiaouDesk reads it with evdev)."""
    qmp('input-send-event', events=[{'type': 'abs', 'data': {'axis': 'x', 'value': x * 32767 // max(1, W - 1)}},
                                    {'type': 'abs', 'data': {'axis': 'y', 'value': y * 32767 // max(1, H - 1)}}])
    time.sleep(0.35)
def click(x=None, y=None, button='left'):
    if x is not None: goto(x, y)
    qmp('input-send-event', events=[{'type': 'btn', 'data': {'button': button, 'down': True}}]); time.sleep(0.12)
    qmp('input-send-event', events=[{'type': 'btn', 'data': {'button': button, 'down': False}}]); time.sleep(0.5)

def press(down):
    qmp('input-send-event', events=[{'type': 'btn', 'data': {'button': 'left', 'down': down}}]); time.sleep(0.1)
def drag(x0, y0, x1, y1):
    goto(x0, y0); press(True)
    for i in range(1, 7): goto(x0 + (x1 - x0) * i // 6, y0 + (y1 - y0) * i // 6)
    press(False); time.sleep(0.4)
def dblclick(x, y):
    goto(x, y); press(True); press(False); time.sleep(0.08); press(True); press(False); time.sleep(0.6)

# ---------------------------------------------------------------- boot
t0 = time.time(); geom = None
while time.time() - t0 < (240 if args.iso else 120):
    if proc.poll() is not None: fail('QEMU died during boot')
    txt = log_text()
    if 'MIAOUDESK_GUI_OK' in txt: break
    if 'MIAOUDESK_ERR' in txt or 'MIAOUDESK_GUI_EXIT' in txt: fail('desktop failed to start:\n' + txt[-800:])
    time.sleep(0.5)
else: fail('graphical MiaouDesk did not start within 120 s')
for line in log_text().splitlines():
    if 'MIAOUDESK_GUI_OK' in line:
        geom = line.split()[1]; scale = int(line.split('scale=')[1])
W, H = (int(v) for v in geom.split('x'))
print(f'Desktop up after {time.time() - t0:.1f}s, mode {W}x{H}, ui scale {scale}%')
def wait_marker(m, timeout=30):
    t1 = time.time()
    while time.time() - t1 < timeout:
        if m in log_text(): return
        time.sleep(0.3)
    fail('serial marker not seen: ' + m)
if args.scenario == 'iso':
    wait_marker('MIAOUDESK_LOGIN_SCREEN'); time.sleep(3); shot('01-login-screen')
elif args.scenario == 'session':
    pass
elif args.scenario == 'full' and not args.quick:
    wait_marker('MIAOUDESK_LOGIN_SCREEN'); time.sleep(2.5); shot('00-login')
    typ('oops\n'); time.sleep(0.3); shot('00b-login-wrong-password'); wait_marker('MIAOUDESK_LOGIN_FAIL'); time.sleep(0.8)
    typ('miaou\n'); wait_marker('MIAOUDESK_LOGIN_OK miaou'); time.sleep(1.5)
    time.sleep(4)
    shot('01-desktop')
else:
    time.sleep(6)                                           # let the DHCP lease arrive and the splash toast settle
    shot('01-desktop')

def S(n): return (n * scale + 50) // 100
# Plasma-style layout (mirrors src/desk/desk.c): panel at the bottom, launcher on the left, one task icon per app
PANEL, TH_H = S(44), S(36)
AH = H - PANEL                                         # work area height
def launcher_xy(): return S(24), H - PANEL // 2
def dock_xy(i): return S(52) + i * S(48) + S(24), H - PANEL // 2
def opened(app): return f'MIAOUDESK_APP_OPEN {app}' in log_text()

def desktop_selection():
    # rubber-band selection with the real pointer, then the context menu on a selected icon
    goto(S(260), S(30)); press(True)
    for i in range(1, 6): goto(S(260) + (S(40) - S(260)) * i // 5, S(30) + (S(250) - S(30)) * i // 5)
    shot('01b-selection-band'); press(False); time.sleep(0.4); shot('01c-selected')
    click(S(16) + S(46), S(16) + S(96) + S(45), button='right'); time.sleep(0.5); shot('01d-context-item')
    key('esc'); time.sleep(0.4)
    click(S(600), S(500)); time.sleep(0.4)                     # click on empty desktop clears the selection

def tools_scenario():
    def launch(name):                     # real launcher: open the menu, type to search, Enter
        click(*launcher_xy()); time.sleep(0.5); typ(name); time.sleep(0.4); key('ret'); time.sleep(1.5)
    launch('notes'); typ('Bonjour MiaouOS\nCeci est un vrai editeur.'); time.sleep(0.5); key('ctrl', 's'); time.sleep(1.0); shot('20-notes')
    launch('calc'); typ('12*(3+4)'); time.sleep(0.6); shot('21-calculator')
    key('ret'); time.sleep(0.5); shot('21b-calculator-result')
    launch('horloge'); time.sleep(1.0); shot('22-clock')
    key('meta_l', 'd'); time.sleep(1.0)
    dblclick(S(16) + S(150), S(16) + S(96) * 0 + S(45))                    # desktop item 'Bienvenue.txt' (second column, first row)
    time.sleep(1.5); shot('23-open-text-from-desktop')
    key('meta_l', 'd'); time.sleep(0.8)
    dblclick(S(16) + S(150), S(16) + S(96) + S(45)); time.sleep(2.0); shot('24-viewer-from-desktop')
    # run dialog (Super+Space): calculator result, then the control centre (click the clock), workspaces, window menu, PrintScreen
    key('meta_l', 'spc'); time.sleep(0.6); typ('2+3*4'); time.sleep(0.6); shot('25-run-dialog'); key('esc'); time.sleep(0.5)
    click(W - S(40), H - PANEL // 2); time.sleep(0.7); shot('26-control-centre'); click(W - S(40), H - PANEL // 2); time.sleep(0.5)
    key('ctrl', 'alt', 'right'); time.sleep(0.8); shot('27-workspace-2'); key('ctrl', 'alt', 'left'); time.sleep(0.6)
    launch('notes'); time.sleep(0.8)
    off_ = 4 * S(28)                                   # cascade offset: this is the 5th window
    right_x, right_y = (W - S(860)) // 2 + off_ - S(40) + S(300), max(0, (AH - S(600)) // 2 + off_ - S(20)) + S(18)
    click(right_x, right_y, button='right'); time.sleep(0.6); shot('28-window-menu'); key('esc'); time.sleep(0.4)
    key('print'); time.sleep(4.0)
    if 'MIAOUDESK_SCREENSHOT' not in log_text(): fail('PrintScreen did not write a screenshot')

def apps2_scenario():
    def launch(name):
        click(*launcher_xy()); time.sleep(0.5); typ(name); time.sleep(0.4); key('ret'); time.sleep(1.5)
    # --- Files: copy a folder from the desktop folder to Images, trash it, look into the trash
    key('meta_l', 'e'); time.sleep(1.5); key('meta_l', 'up'); time.sleep(0.8)
    def place(i): click(100, TH_H + S(34) + i * S(38) + S(17)); time.sleep(0.6)
    def row(i): click(500, TH_H + S(52) + S(28) + i * S(32) + S(16)); time.sleep(0.4)
    place(1); row(0); shot('30-files-desktop-folder'); key('ctrl', 'c'); time.sleep(0.3)
    place(3); key('ctrl', 'v'); time.sleep(1.0); shot('31-files-pasted')
    row(0); key('delete'); time.sleep(1.0); place(4); time.sleep(0.8); shot('32-files-trash')
    # --- Monitor: disks and system log
    launch('moniteur'); key('meta_l', 'up'); time.sleep(1.0)
    click(S(16) + S(118) + S(55), TH_H + S(16) * 2 + S(150) + S(17)); time.sleep(0.8); shot('33-monitor-disks')
    click(S(16) + 2 * S(118) + S(55), TH_H + S(16) * 2 + S(150) + S(17)); time.sleep(2.0); shot('34-monitor-journal')
    # --- Settings: accounts and shortcuts
    launch('param'); key('meta_l', 'up'); time.sleep(1.0)
    click(100, TH_H + S(10) + 4 * S(40) + S(18)); time.sleep(0.8); shot('35-settings-accounts')
    click(100, TH_H + S(10) + 6 * S(40) + S(18)); time.sleep(0.8); shot('36-settings-shortcuts')
    click(100, TH_H + S(10) + 3 * S(40) + S(18)); time.sleep(0.8); shot('37-settings-datetime')
    # --- Terminal: selection copy + paste
    key('meta_l', 't'); time.sleep(2.0); typ('echo copie-moi\n'); time.sleep(0.8)
    shot('38-terminal-before-select')

def session_scenario():
    wait_marker('MIAOUDESK_LOGIN_SCREEN'); time.sleep(2.0)
    key('tab'); time.sleep(0.6); shot('40-login-guest'); key('ret'); wait_marker('MIAOUDESK_LOGIN_OK invite'); time.sleep(3.0); shot('41-guest-desktop')
    key('meta_l', 't'); time.sleep(2.5); typ('id\n'); time.sleep(0.8); typ('pwd\n'); time.sleep(0.8); typ('ls /root\n'); time.sleep(0.8); shot('42-guest-terminal')
    key('meta_l', 'l'); time.sleep(1.8); shot('43-lock-screen')
    key('tab'); time.sleep(0.5); typ('miaou\n'); wait_marker('MIAOUDESK_LOGIN_OK miaou'); time.sleep(3.0); shot('44-switched-to-miaou')

def web_scenario():
    # needs tests/web/server.py running on the host (guest reaches it at 10.0.2.2:8000)
    def launch(name):
        click(*launcher_xy()); time.sleep(0.5); typ(name); time.sleep(0.4); key('ret'); time.sleep(1.5)
    launch('terminal'); typ('wget -T 5 -O - http://10.0.2.2:8000/style.css\n'); time.sleep(8); shot('59-wget-check'); key('ctrl', 'd'); time.sleep(0.8)
    launch('navigateur'); time.sleep(1.0); shot('60-browser-home')
    key('meta_l', 'up'); time.sleep(1.2)
    key('ctrl', 'l'); time.sleep(0.4); typ('http://10.0.2.2:8000/index.html\n'); time.sleep(8.0); shot('61-web-page')
    bx, by = S(60), TH_H + S(52) + S(8) + S(16)
    click(bx, by); time.sleep(0.6); click(bx, by); time.sleep(0.6); shot('62-web-js-clicked')
    click(S(200), by); time.sleep(0.4); typ('chat'); time.sleep(0.5); shot('63-web-typed')
    key('ret'); time.sleep(6.0); shot('64-web-form-result')
    key('ctrl', 'l'); time.sleep(0.4); typ('http://10.0.2.2:8000/modern.html\n'); time.sleep(8.0); shot('65-web-modern')
    bx2, by2 = S(1100), TH_H + S(52) + S(260); click(bx2, by2); time.sleep(1.5); shot('66-web-modern-clicked')

if args.scenario == 'apps2':
    apps2_scenario()
elif args.scenario == 'session':
    session_scenario()
elif args.scenario == 'web':
    web_scenario()
elif args.scenario == 'tools':
    tools_scenario()
elif args.scenario == 'desk':
    desktop_selection()
elif args.scenario == 'iso':
    time.sleep(3)
elif args.scenario == 'fr':
    key('meta_l', 't'); time.sleep(2.0)
    if not opened('terminal'): fail('terminal did not open (Super+T)')
    typ('echo qw\n'); time.sleep(1.0)               # physical keys q,w -> AZERTY a,z
    shot('01-azerty-terminal')
elif args.scenario == 'res':
    key('meta_l', 'e'); time.sleep(2.0); shot('01-res-files')
elif not args.quick:
    desktop_selection()
    # --- start menu via the real mouse
    click(*launcher_xy()); time.sleep(0.6); shot('02-menu'); key('esc')
    # --- Terminal from the dock, then real commands through the keyboard
    click(*dock_xy(0)); time.sleep(2.0)
    if not opened('terminal'): fail('terminal did not open from the dock')
    typ('uname -a\n'); time.sleep(0.8); typ('ls /\n'); time.sleep(0.8); typ('ping -c 2 10.0.2.2\n'); time.sleep(4); typ('cat /etc/os-release\n'); time.sleep(1.0)
    shot('03-terminal')
    # --- window management with the real pointer: drag, resize from the corner, snap, minimise/restore, maximise
    th = TH_H; tw_ = min(S(820), W - S(20)); tw_h = min(S(520), AH - S(10))
    wx, wy = (W - tw_) // 2 - S(40), max(0, (AH - tw_h) // 2 - S(20))
    drag(wx + tw_ // 2, wy + th // 2, wx + tw_ // 2 - S(120), wy + th // 2 + S(50))          # move
    wx -= S(120); wy += S(50)
    drag(wx + tw_ - 2, wy + tw_h - 2, wx + tw_ + S(60), wy + tw_h + S(30))                    # resize (bottom-right)
    tw_ += S(62); tw_h += S(32)
    shot('03b-window-moved-resized')
    key('meta_l', 'left'); time.sleep(0.8); shot('03c-snapped-left')
    click(W // 2 - S(24) - 2 * S(32), th // 2); time.sleep(0.6)                                # minimise button of the snapped window
    shot('03d-minimised')
    click(*dock_xy(0)); time.sleep(0.8)                                                        # dock icon restores it
    dblclick(W // 4, th // 2)                                                                  # snapped window: double-click restores it
    dblclick(W // 4, wy + th // 2); time.sleep(0.6); shot('03e-maximised-by-doubleclick')      # floating window: double-click maximises
    dblclick(W // 4, th // 2); time.sleep(0.6)                                                 # maximised: double-click restores
    # --- Files (Super+E), maximised (Super+Up)
    key('meta_l', 'e'); time.sleep(1.5); key('meta_l', 'up'); time.sleep(0.8)
    if not opened('files'): fail('files did not open (Super+E)')
    click(100, TH_H + S(34) + S(38) + S(10))            # sidebar: "Racine"
    time.sleep(0.8); shot('04-files')
    # --- Browser
    click(*dock_xy(2)); time.sleep(1.5); key('meta_l', 'up'); time.sleep(0.8)
    if not opened('browser'): fail('browser did not open')
    shot('05-browser')
    click(S(300), TH_H + S(26)); typ('file:///etc/os-release\n'); time.sleep(1.5)
    shot('05b-browser-local-page')
    # --- Store: select the first package and install it for real
    click(*dock_xy(3)); time.sleep(1.5); key('meta_l', 'up'); time.sleep(0.8)
    if not opened('store'): fail('store did not open')
    cy0 = TH_H
    click(S(200), cy0 + S(18) + S(68) + S(20)); time.sleep(0.6)
    shot('06-store-selected')
    ch_store = AH - TH_H - 1
    by = cy0 + ch_store - S(176)
    click(S(18) + S(70), by + S(19)); time.sleep(5)      # Install (real miaou-get run inside the guest)
    shot('07-store-installed')
    click(S(18) + S(70), by + S(19)); time.sleep(2.5)    # Launch the installed package
    shot('08-store-run')
    # --- Monitor, Network, Settings
    click(*dock_xy(4)); time.sleep(2.0); key('meta_l', 'up'); time.sleep(1.0); shot('09-monitor')
    click(*dock_xy(5)); time.sleep(2.0); key('meta_l', 'up'); time.sleep(1.0); shot('10-network')
    click(*dock_xy(6)); time.sleep(1.5); key('meta_l', 'up'); time.sleep(1.0); shot('11-settings')
    # --- light theme through the desktop context menu (right click), then bring the windows back
    key('meta_l', 'd'); time.sleep(1.0)
    cx_, cy_ = W // 2, H // 3
    click(cx_, cy_, button='right'); time.sleep(0.6); shot('11b-context-menu')
    click(cx_ + S(40), cy_ + S(6) + 3 * S(32) + S(16)); time.sleep(1.0)      # "Changer de thème"
    shot('13-desktop-light')
    key('meta_l', 'd'); time.sleep(1.0); shot('12-settings-light')

end = log_text()
(serial.parent / (serial.stem + '-final.log') if args.serial_log else build / 'qemu-serial.log').write_text(end)
try:
    stream.write(b'{"execute":"quit"}\n')
except Exception: pass
cleanup()

# ---------------------------------------------------------------- verification
errors = []
for m in ('MIAOUOS_BOOT_OK', 'MIAOUDESK_GUI_OK'):
    if m not in end: errors.append('missing serial marker ' + m)
if 'MIAOUDESK_GUI_EXIT' in end: errors.append('desktop exited unexpectedly')
hashes = {}
for p in taken:
    h = hashlib.sha256(p.read_bytes()).hexdigest(); hashes.setdefault(h, []).append(p.name)
    if p.stat().st_size < 20000: errors.append(f'{p.name} suspiciously small')
for h, names in hashes.items():
    if len(names) > 1 and not args.quick and args.scenario == 'full': errors.append('identical screenshots: ' + ', '.join(names))
print('serial markers:', [l for l in end.splitlines() if l.startswith('MIAOU')])
if errors:
    print('FAIL:', *errors, sep='\n  ', file=sys.stderr); sys.exit(1)
print(f'OK: {len(taken)} real screenshots in {shots}')
