#!/usr/bin/env bash
# Boot the kernel + initramfs in QEMU, drive the desktop (mouse + keyboard through QMP) and capture real PNG screenshots.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
mkdir -p "$BUILD/screenshots"
for file in "$BUILD/miaouos-kernel-bzImage" "$BUILD/miaouos-initramfs.cpio.gz"; do
  [[ -s "$file" ]] || { printf 'Missing build output: %s\n' "$file" >&2; exit 1; }
done
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo 'qemu-system-x86_64 is required' >&2; exit 1; }
rm -f "$BUILD"/screenshots/*.png
# 1. full desktop scenario incl. login (US keyboard: scripted typing), 2. AZERTY, 3. a smaller screen (real kernel video= mode),
# 4. everyday tools, 5. files / monitor / settings, 6. user sessions (guest, lock screen, user switch)
python3 "$ROOT/scripts/qemu_test.py" --root "$ROOT"
python3 "$ROOT/scripts/qemu_test.py" --root "$ROOT" --scenario fr --kbd fr --tag fr- --serial-log "$BUILD/serial-fr.log"
python3 "$ROOT/scripts/qemu_test.py" --root "$ROOT" --scenario res --res 1024x768 --tag r1024- --serial-log "$BUILD/serial-1024.log"
python3 "$ROOT/scripts/qemu_test.py" --root "$ROOT" --scenario tools --tag tools- --serial-log "$BUILD/serial-tools.log"
python3 "$ROOT/scripts/qemu_test.py" --root "$ROOT" --scenario apps2 --tag apps2- --serial-log "$BUILD/serial-apps2.log"
python3 "$ROOT/scripts/qemu_test.py" --root "$ROOT" --scenario session --tag session- --serial-log "$BUILD/serial-session.log"
ls -la "$BUILD/screenshots"
