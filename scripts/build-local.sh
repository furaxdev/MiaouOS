#!/usr/bin/env bash
# Local build (no CI): rebuilds the userland on top of a cached kernel + base initramfs, makes the bootable ISO
# and packages MiaouOS-<commit>.zip (ISO, launch scripts, README, provenance, screenshots).
#
# Needs once: build/miaouos-kernel-bzImage (kernel built by scripts/build.sh) and
#             build/base-initramfs.cpio.gz (BusyBox base rootfs produced by scripts/build.sh, kept as a cache).
# Usage: scripts/build-local.sh [--no-zip]
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"; WORK="${TMPDIR:-/tmp}/miaou-local-work"; ROOTFS="$WORK/rootfs"
KERNEL_IMG="${KERNEL_IMG:-$BUILD/miaouos-kernel-bzImage}"
BASE="${BASE_INITRAMFS:-$BUILD/base-initramfs.cpio.gz}"
for f in "$KERNEL_IMG" "$BASE"; do [[ -s "$f" ]] || { echo "missing $f (see header of this script)" >&2; exit 1; }; done
for t in gcc cpio gzip grub-mkrescue xorriso; do command -v "$t" >/dev/null || { echo "need $t" >&2; exit 1; }; done

rm -rf "$WORK"; mkdir -p "$ROOTFS" "$WORK/build" "$BUILD"
( cd "$ROOTFS" && zcat "$BASE" | cpio -id --quiet 2>/dev/null )
rm -f "$ROOTFS/usr/bin/miaoudesk-gui"
bash "$ROOT/scripts/install-miaou.sh" "$ROOTFS" "$WORK/build" >/dev/null
( cd "$ROOTFS" && find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9 ) > "$BUILD/miaouos-initramfs.cpio.gz.new"
mv "$BUILD/miaouos-initramfs.cpio.gz.new" "$BUILD/miaouos-initramfs.cpio.gz"
[[ "$KERNEL_IMG" -ef "$BUILD/miaouos-kernel-bzImage" ]] || cp "$KERNEL_IMG" "$BUILD/miaouos-kernel-bzImage"

ISO_ROOT="$WORK/iso-root"; mkdir -p "$ISO_ROOT/boot/grub"
cp "$BUILD/miaouos-kernel-bzImage" "$BUILD/miaouos-initramfs.cpio.gz" "$ISO_ROOT/boot/"
cat > "$ISO_ROOT/boot/grub/grub.cfg" <<'GRUB'
set timeout=2
set default=0
menuentry "MiaouOS — Le Linux des chats !" {
    linux /boot/miaouos-kernel-bzImage console=tty0 console=ttyS0,115200n8 rdinit=/init panic=5 quiet loglevel=3 vt.global_cursor_default=0 miaou.kbd=fr
    initrd /boot/miaouos-initramfs.cpio.gz
}
GRUB
grub-mkrescue -o "$BUILD/miaouos.iso" "$ISO_ROOT" >/dev/null 2>&1
( cd "$ROOT" && sha256sum build/miaouos-kernel-bzImage build/miaouos-initramfs.cpio.gz build/miaouos.iso ) > "$BUILD/SHA256SUMS"
COMMIT="$(git -C "$ROOT" rev-parse HEAD)"; SHORT="${COMMIT:0:7}"
DIRTY=""; [[ -n "$(git -C "$ROOT" status --porcelain -- src assets scripts tools)" ]] && DIRTY=" (+ uncommitted changes)"
{ echo "commit=$COMMIT$DIRTY"; echo "ref=$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)"; echo "built=local"; echo "built_at_utc=$(date -u +%FT%TZ)"; cat "$BUILD/SHA256SUMS"; } > "$BUILD/PROVENANCE.txt"
echo "ISO: $BUILD/miaouos.iso  ($SHORT$DIRTY)"
[[ "${1:-}" == "--no-zip" ]] && exit 0

PKG="$WORK/pkg"; rm -rf "$PKG"; mkdir -p "$PKG/screenshots"
cp "$BUILD/miaouos.iso" "$BUILD/SHA256SUMS" "$BUILD/PROVENANCE.txt" "$PKG/"
cat > "$PKG/lancer-miaouos.sh" <<'SH'
#!/usr/bin/env bash
# Lance MiaouOS (ISO BIOS/GRUB) dans QEMU x86_64.  Usage : ./lancer-miaouos.sh [miaouos.iso]
ISO="${1:-miaouos.iso}"
exec qemu-system-x86_64 -machine pc -m 1024 -smp 2 -accel kvm -accel tcg \
  -cdrom "$ISO" -boot d -vga std -display gtk \
  -device virtio-tablet-pci -device e1000,netdev=n0 -netdev user,id=n0
SH
chmod +x "$PKG/lancer-miaouos.sh"
printf '@echo off\r\nrem Lance MiaouOS dans QEMU (QEMU doit etre installe et dans le PATH)\r\nqemu-system-x86_64 -machine pc -m 1024 -smp 2 -accel whpx -accel tcg -cdrom miaouos.iso -boot d -vga std -device virtio-tablet-pci -device e1000,netdev=n0 -netdev user,id=n0\r\n' > "$PKG/lancer-miaouos.bat"
cat > "$PKG/LISEZ-MOI.txt" <<TXT
MiaouOS - commit $SHORT$DIRTY (build local)

Lancer :   ./lancer-miaouos.sh miaouos.iso      (Linux/macOS)   ou   lancer-miaouos.bat (Windows, QEMU installe)
Connexion : utilisateur "miaou", mot de passe "miaou"  (ou compte "invite", sans mot de passe)
Les comptes et fichiers vivent en RAM : ils disparaissent au redemarrage.
PROVENANCE.txt : commit et empreintes SHA-256. screenshots/ : captures reelles prises dans QEMU (build local).
TXT
cp "$BUILD"/screenshots/*.png "$PKG/screenshots/" 2>/dev/null || true
OUT="$BUILD/MiaouOS-$SHORT.zip"; rm -f "$OUT"; ( cd "$PKG" && zip -qr "$OUT" . )
echo "ZIP: $OUT"
