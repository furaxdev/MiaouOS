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
ap.add_argument('--scenario', default='full', choices=['full', 'fr', 'res', 'iso'])
args = ap.parse_args()
root = Path(args.root); build = root / 'build'; shots = build / 'screenshots'
shots.mkdir(parents=True, exist_ok=True)
qmp_path = build / 'qmp.sock'; serial = Path(args.serial_log) if args.serial_log else build / 'serial.log'
kernel = args.kernel or str(build / 'miaouos-kernel-bzImage')
initrd = args.initrd or str(build / 'miaouos-initramfs.cpio.gz')
for p in (qmp_path, serial):
    try: p.unlink()
    except FileNotFoundError: pass
append = 'console=tty0 console=ttyS0,115200n8 rdinit=/init panic=5 miaou.kbd=%s miaou.theme=%s' % (args.kbd, args.theme)
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
def typ(text):
    for c in text:
        if c == ':': key('shift', 'semicolon')
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
time.sleep(6)                                           # let the DHCP lease arrive and the splash toast settle
shot('01-desktop')

def S(n): return (n * scale + 50) // 100
PH, DOCK_ICON = S(34), S(46)
n_dock = 7; gap = S(10); dw = n_dock * DOCK_ICON + (n_dock - 1) * gap + S(28); dh = DOCK_ICON + S(20)
dx, dy = (W - dw) // 2, H - dh - S(10)
def dock_xy(i): return dx + S(14) + i * (DOCK_ICON + gap) + DOCK_ICON // 2, dy + S(10) + DOCK_ICON // 2
def opened(app): return f'MIAOUDESK_APP_OPEN {app}' in log_text()

if args.scenario == 'iso':
    time.sleep(3)
elif args.scenario == 'fr':
    key('meta_l', 't'); time.sleep(2.0)
    if not opened('terminal'): fail('terminal did not open (Super+T)')
    typ('echo qw\n'); time.sleep(1.0)               # physical keys q,w -> AZERTY a,z
    shot('01-azerty-terminal')
elif args.scenario == 'res':
    key('meta_l', 'e'); time.sleep(2.0); shot('01-res-files')
elif not args.quick:
    # --- start menu via the real mouse
    click(S(50), PH // 2); time.sleep(0.5); shot('02-menu'); key('esc')
    # --- Terminal from the dock, then real commands through the keyboard
    click(*dock_xy(0)); time.sleep(2.0)
    if not opened('terminal'): fail('terminal did not open from the dock')
    typ('uname -a\n'); time.sleep(0.8); typ('ls /\n'); time.sleep(0.8); typ('ping -c 2 10.0.2.2\n'); time.sleep(4); typ('cat /etc/os-release\n'); time.sleep(1.0)
    shot('03-terminal')
    # --- Files (Super+E), maximised (Super+Up)
    key('meta_l', 'e'); time.sleep(1.5); key('meta_l', 'up'); time.sleep(0.8)
    if not opened('files'): fail('files did not open (Super+E)')
    click(100, PH + S(38) + S(34) + 38 * 1 + 10)       # sidebar: "Racine"
    time.sleep(0.8); shot('04-files')
    # --- Browser
    click(*dock_xy(2)); time.sleep(1.5); key('meta_l', 'up'); time.sleep(0.8)
    if not opened('browser'): fail('browser did not open')
    shot('05-browser')
    click(S(300), PH + S(38) + S(26)); typ('file:///etc/os-release\n'); time.sleep(1.5)
    shot('05b-browser-local-page')
    # --- Store: select the first package and install it for real
    click(*dock_xy(3)); time.sleep(1.5); key('meta_l', 'up'); time.sleep(0.8)
    if not opened('store'): fail('store did not open')
    cy0 = PH + S(38)
    click(S(200), cy0 + S(18) + S(68) + S(20)); time.sleep(0.6)
    shot('06-store-selected')
    dock_h = DOCK_ICON + S(22); ch_store = (H - PH - dock_h - S(14)) - S(38) - 1
    by = cy0 + ch_store - S(176)
    click(S(18) + S(70), by + S(19)); time.sleep(5)      # Install (real miaou-get run inside the guest)
    shot('07-store-installed')
    click(S(18) + S(70), by + S(19)); time.sleep(2.5)    # Launch the installed package
    shot('08-store-run')
    # --- Monitor, Network, Settings
    click(*dock_xy(4)); time.sleep(2.0); key('meta_l', 'up'); time.sleep(1.0); shot('09-monitor')
    click(*dock_xy(5)); time.sleep(2.0); key('meta_l', 'up'); time.sleep(1.0); shot('10-network')
    click(*dock_xy(6)); time.sleep(1.5); key('meta_l', 'up'); time.sleep(1.0); shot('11-settings')
    # --- light theme through the tray toggle, back to desktop
    tray_theme_x = W - S(10) - (S(36)) - S(4) - S(18)
    click(tray_theme_x, PH // 2); time.sleep(1.0); shot('12-settings-light')
    key('meta_l', 'd'); time.sleep(1.0); shot('13-desktop-light')

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
