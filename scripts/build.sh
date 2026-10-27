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
for tool in gcc make wget tar xz cpio gzip; do need "$tool"; done

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
  yes '' | make -C "$BB" oldconfig
  make -C "$BB" -j"$JOBS"
fi

ROOTFS="$WORK/rootfs"
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"/{bin,sbin,etc,proc,sys,dev,dev/pts,tmp,run,usr/bin,usr/sbin,root}
chmod 1777 "$ROOTFS/tmp"
make -C "$BB" CONFIG_PREFIX="$ROOTFS" install
install -D -m 0755 "$ROOT/src/init" "$ROOTFS/init"
install -D -m 0755 "$ROOT/src/miaoudesk.sh" "$ROOTFS/usr/bin/miaoudesk"
ln -sf /usr/bin/miaoudesk "$ROOTFS/bin/miaoudesk"
cat > "$ROOTFS/etc/issue" <<'ISSUE'
MiaouOS 0.1 - Le Linux des chats !
ISSUE

(
  cd "$ROOTFS"
  find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9
) > "$BUILD/miaouos-initramfs.cpio.gz"
cp "$KERNEL/arch/x86/boot/bzImage" "$BUILD/miaouos-kernel-bzImage"
sha256sum "$BUILD/miaouos-kernel-bzImage" "$BUILD/miaouos-initramfs.cpio.gz" > "$BUILD/SHA256SUMS"
printf 'Kernel and initramfs built.\n'
