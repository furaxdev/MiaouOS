#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO="$ROOT/build/miaouos.iso"
LOG="$ROOT/build/iso-serial.log"
[[ -s "$ISO" ]] || { echo "ISO missing: $ISO" >&2; exit 1; }
rm -f "$LOG"
qemu-system-x86_64 -machine pc -m 512M -smp 2 -cdrom "$ISO" -boot d -display none -serial "file:$LOG" -monitor none -no-reboot -no-shutdown &
pid=$!
cleanup() { kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; }
trap cleanup EXIT
for _ in $(seq 1 60); do
  if grep -q 'MIAOUOS_BOOT_OK' "$LOG" 2>/dev/null && grep -q 'MIAOUDESK_GUI_OK' "$LOG" 2>/dev/null; then
    echo "Bootable ISO and graphical MiaouDesk passed"
    exit 0
  fi
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "QEMU exited before MiaouOS boot marker." >&2
    cat "$LOG" >&2 || true
    exit 1
  fi
  sleep 1
done
echo "ISO boot marker timeout." >&2
cat "$LOG" >&2 || true
exit 1
