#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
SRC="$BUILD/sources"
WORK="$BUILD/work"
mkdir -p "$SRC" "$WORK" "$BUILD/screenshots"
JOBS="${JOBS:-2}"
KERNEL_VERSION=6.12.30
BUSYBOX_VERSION=1.37.0

need() { command -v "$1" >/dev/null 2>&1 || { printf 'Missing required tool: %s\n' "$1" >&2; exit 1; }; }
for tool in gcc make wget tar xz cpio gzip grub-mkrescue xorriso; do need "$tool"; done

fetch() {
  local url="$1" out="$2"
  if [[ ! -s "$out" ]]; then wget --https-only -O "$out" "$url"; fi
}

fetch "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_VERSION}.tar.xz" "$SRC/linux-${KERNEL_VERSION}.tar.xz"
fetch "https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2" "$SRC/busybox-${BUSYBOX_VERSION}.tar.bz2"

[[ -d "$WORK/linux-${KERNEL_VERSION}" ]] || tar -xf "$SRC/linux-${KERNEL_VERSION}.tar.xz" -C "$WORK"
[[ -d "$WORK/busybox-${BUSYBOX_VERSION}" ]] || tar -xf "$SRC/busybox-${BUSYBOX_VERSION}.tar.bz2" -C "$WORK"

KERNEL="$WORK/linux-${KERNEL_VERSION}"
BB="$WORK/busybox-${BUSYBOX_VERSION}"

if [[ ! -s "$KERNEL/arch/x86/boot/bzImage" ]]; then
  make -C "$KERNEL" defconfig
  "$KERNEL/scripts/config" --file "$KERNEL/.config" \
    --enable CONFIG_BLK_DEV_INITRD \
    --enable CONFIG_DEVTMPFS --enable CONFIG_DEVTMPFS_MOUNT \
    --enable CONFIG_PROC_FS --enable CONFIG_SYSFS --enable CONFIG_TMPFS \
    --enable CONFIG_TTY --enable CONFIG_VT --enable CONFIG_VT_CONSOLE \
    --enable CONFIG_SERIAL_8250 --enable CONFIG_SERIAL_8250_CONSOLE \
    --enable CONFIG_BINFMT_ELF --enable CONFIG_RD_GZIP \
    --enable CONFIG_DRM --enable CONFIG_DRM_BOCHS --enable CONFIG_DRM_SIMPLEDRM \
    --enable CONFIG_FB --enable CONFIG_FRAMEBUFFER_CONSOLE
  make -C "$KERNEL" olddefconfig
  make -C "$KERNEL" -j"$JOBS" bzImage
fi

if [[ ! -x "$BB/busybox" ]]; then
  make -C "$BB" defconfig
  sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$BB/.config"
  sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' "$BB/.config"
  make -C "$BB" oldconfig </dev/null
  make -C "$BB" -j"$JOBS"
fi

ROOTFS="$WORK/rootfs"
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"/{bin,sbin,etc,proc,sys,dev,dev/pts,tmp,run,usr/bin,usr/sbin,root}
chmod 1777 "$ROOTFS/tmp"
make -C "$BB" CONFIG_PREFIX="$ROOTFS" install
install -D -m 0755 "$ROOT/src/init" "$ROOTFS/init"
install -D -m 0755 "$ROOT/src/miaoudesk.sh" "$ROOTFS/usr/bin/miaoudesk"
install -D -m 0755 "$ROOT/src/usr/bin/miaou" "$ROOTFS/usr/bin/miaou"
install -D -m 0755 "$ROOT/src/usr/bin/miaou-get" "$ROOTFS/usr/bin/miaou-get"
install -D -m 0644 "$ROOT/src/usr/lib/miaou/package-engine.sh" "$ROOTFS/usr/lib/miaou/package-engine.sh"
install -D -m 0644 "$ROOT/src/usr/lib/miaou/ui.sh" "$ROOTFS/usr/lib/miaou/ui.sh"
install -D -m 0644 "$ROOT/src/usr/lib/miaou/cursors.sh" "$ROOTFS/usr/lib/miaou/cursors.sh"
install -D -m 0644 "$ROOT/src/usr/lib/miaou/neko.sh" "$ROOTFS/usr/lib/miaou/neko.sh"
install -D -m 0755 "$ROOT/src/usr/bin/miaou-cursors" "$ROOTFS/usr/bin/miaou-cursors"
install -D -m 0755 "$ROOT/src/usr/bin/miaou-neko" "$ROOTFS/usr/bin/miaou-neko"
install -D -m 0755 "$ROOT/src/usr/bin/miaou-settings" "$ROOTFS/usr/bin/miaou-settings"
install -D -m 0755 "$ROOT/src/usr/bin/miaou-files" "$ROOTFS/usr/bin/miaou-files"
install -D -m 0755 "$ROOT/src/usr/bin/miaou-monitor" "$ROOTFS/usr/bin/miaou-monitor"
install -D -m 0755 "$ROOT/src/usr/bin/miaou-network" "$ROOTFS/usr/bin/miaou-network"
install -D -m 0755 "$ROOT/src/usr/bin/miaou-run" "$ROOTFS/usr/bin/miaou-run"
install -D -m 0755 "$ROOT/src/usr/bin/miaou-apps" "$ROOTFS/usr/bin/miaou-apps"
mkdir -p "$ROOTFS/var/lib/miaou/repository" "$ROOTFS/var/cache/miaou" "$ROOTFS/opt/miaou/packages" "$ROOTFS/usr/share/miaou/packages"
cp "$ROOT/packages/miaou-welcome/manifest" "$ROOTFS/var/lib/miaou/repository/miaou-welcome.manifest"
mkdir -p "$WORK/miaou-welcome-package"
cp "$ROOT/packages/miaou-welcome/manifest" "$WORK/miaou-welcome-package/manifest"
cp -a "$ROOT/packages/miaou-welcome/payload" "$WORK/miaou-welcome-package/payload"
chmod 0755 "$WORK/miaou-welcome-package/payload/usr/bin/miaou-welcome"
tar -czf "$ROOTFS/usr/share/miaou/packages/miaou-welcome.miaou" -C "$WORK/miaou-welcome-package" manifest payload
ln -sf /usr/bin/miaoudesk "$ROOTFS/bin/miaoudesk"
ln -sf /usr/bin/miaou "$ROOTFS/bin/miaou"
ln -sf /usr/bin/miaou-get "$ROOTFS/bin/miaou-get"
cat > "$ROOTFS/etc/issue" <<'ISSUE'
MiaouOS 0.1 - Le Linux des chats !
ISSUE

(
  cd "$ROOTFS"
  find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9
) > "$BUILD/miaouos-initramfs.cpio.gz"
cp "$KERNEL/arch/x86/boot/bzImage" "$BUILD/miaouos-kernel-bzImage"
ISO_ROOT="$WORK/iso-root"
rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT/boot/grub"
cp "$BUILD/miaouos-kernel-bzImage" "$ISO_ROOT/boot/miaouos-kernel-bzImage"
cp "$BUILD/miaouos-initramfs.cpio.gz" "$ISO_ROOT/boot/miaouos-initramfs.cpio.gz"
cat > "$ISO_ROOT/boot/grub/grub.cfg" <<'GRUB'
set timeout=5
set default=0
menuentry "MiaouOS — Le Linux des chats !" {
    linux /boot/miaouos-kernel-bzImage console=tty0 console=ttyS0,115200n8 rdinit=/init panic=5
    initrd /boot/miaouos-initramfs.cpio.gz
}
GRUB
grub-mkrescue -o "$BUILD/miaouos.iso" "$ISO_ROOT" >/dev/null
sha256sum "$BUILD/miaouos-kernel-bzImage" "$BUILD/miaouos-initramfs.cpio.gz" "$BUILD/miaouos.iso" > "$BUILD/SHA256SUMS"
printf 'Kernel, initramfs and bootable BIOS ISO built.\n'
