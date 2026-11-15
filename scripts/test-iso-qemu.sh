#!/usr/bin/env bash
# Boot the generated ISO (BIOS + GRUB) in QEMU, wait for the serial markers, and capture a real screenshot.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO="$ROOT/build/miaouos.iso"
[[ -s "$ISO" ]] || { echo "ISO missing: $ISO" >&2; exit 1; }
rm -f "$ROOT/build/iso-serial.log"
python3 "$ROOT/scripts/qemu_test.py" --root "$ROOT" --iso "$ISO" --scenario iso --tag iso- --serial-log "$ROOT/build/iso-serial.log"
grep -q MIAOUOS_BOOT_OK "$ROOT/build/iso-serial.log" && grep -q MIAOUDESK_GUI_OK "$ROOT/build/iso-serial.log"
echo "Bootable ISO and graphical MiaouDesk passed"
