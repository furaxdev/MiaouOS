import argparse
import os
import socket
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
args = parser.parse_args()
root = Path(args.root)
build = root / 'build'
shots = build / 'screenshots'
shots.mkdir(parents=True, exist_ok=True)
qmp = build / 'qmp.sock'
serial = build / 'serial.log'
screenshot = shots / 'miaouos-boot.png'
for path in (qmp, serial, screenshot):
    try:
        path.unlink()
    except FileNotFoundError:
        pass
cmd = [
    'qemu-system-x86_64', '-machine', 'q35', '-m', '512M', '-smp', '2',
    '-kernel', str(build / 'miaouos-kernel-bzImage'),
    '-initrd', str(build / 'miaouos-initramfs.cpio.gz'),
    '-append', 'console=tty0 console=ttyS0,115200n8 rdinit=/init panic=5',
    '-display', 'gtk', '-vga', 'std', '-serial', f'file:{serial}',
    '-qmp', f'unix:{qmp},server=on,wait=off', '-no-reboot', '-no-shutdown'
]
env = os.environ.copy()
proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)
try:
    deadline = time.time() + 90
    sock = None
    while time.time() < deadline:
        if proc.poll() is not None:
            err = proc.stderr.read().decode(errors='replace') if proc.stderr else ''
            raise RuntimeError(f'QEMU exited early ({proc.returncode}): {err}')
        if qmp.exists():
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.connect(str(qmp))
                break
            except OSError:
                if sock:
                    sock.close()
                sock = None
        time.sleep(0.25)
    if sock is None:
        raise RuntimeError('QEMU QMP socket did not appear')
    stream = sock.makefile('rwb', buffering=0)
    stream.readline()
    stream.write(b'{"execute":"qmp_capabilities"}\n')
    stream.readline()
    time.sleep(12)
    stream.write((f'{{"execute":"screendump","arguments":{{"filename":"{screenshot}"}}}}\n').encode())
    response = stream.readline()
    if b'error' in response.lower():
        raise RuntimeError(f'QMP screendump failed: {response.decode(errors="replace")}')
    time.sleep(2)
    stream.write(b'{"execute":"quit"}\n')
    stream.close()
    sock.close()
    proc.wait(timeout=10)
finally:
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
if not screenshot.exists() or screenshot.stat().st_size < 1000:
    raise RuntimeError('Screenshot missing or unexpectedly small')
log = serial.read_text(errors='replace') if serial.exists() else ''
(build / 'qemu-serial.log').write_text(log)
if 'MIAOUOS_BOOT_OK' not in log:
    raise RuntimeError('Boot marker missing from serial log. See build/qemu-serial.log')
if 'MiaouOS' not in log and 'MIAOUOS_BOOT_OK' not in log:
    raise RuntimeError('Expected MiaouOS text missing from serial log')
print(f'Boot marker found. Screenshot: {screenshot}')
print(f'Serial log: {build / "qemu-serial.log"}')
