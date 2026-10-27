#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
mkdir -p "$BUILD/screenshots"
for file in "$BUILD/miaouos-kernel-bzImage" "$BUILD/miaouos-initramfs.cpio.gz"; do
  [[ -s "$file" ]] || { printf 'Missing build output: %s\n' "$file" >&2; exit 1; }
done
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo 'qemu-system-x86_64 is required' >&2; exit 1; }
python3 "$ROOT/scripts/qemu_test.py" --root "$ROOT"
