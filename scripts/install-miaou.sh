#!/usr/bin/env bash
# Install the MiaouOS userland (desktop, assets, scripts, packages, config) into a root filesystem tree.
# Used by build.sh and by the local test workflow so that tests always exercise the real install path.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOTFS="${1:?usage: install-miaou.sh ROOTFS [WORKDIR]}"
WORK="${2:-$ROOT/build/work}"
mkdir -p "$WORK" "$ROOTFS"/{bin,sbin,etc,proc,sys,dev,tmp,run,root,usr/bin,usr/sbin,usr/share/miaou/{fonts,icons,wallpapers,licenses,packages},usr/share/udhcpc,usr/lib/miaou,var/lib/miaou/repository,var/cache/miaou,opt/miaou/packages}

# --- MiaouDesk (C, static) ---
gcc -std=gnu11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Wno-format-truncation -Wno-missing-field-initializers -static \
  "$ROOT"/src/desk/*.c -o "$ROOTFS/usr/bin/miaoudesk-gui" -lm
# --- assets (fonts, icons, wallpapers) ---
install -m 0644 "$ROOT"/assets/fonts/*.ttf "$ROOTFS/usr/share/miaou/fonts/"
install -m 0644 "$ROOT"/assets/fonts/OFL-*.txt "$ROOTFS/usr/share/miaou/licenses/"
mkdir -p "$ROOTFS/usr/share/miaou/icons/mono"
install -m 0644 "$ROOT"/assets/icons/mono/*.png "$ROOTFS/usr/share/miaou/icons/mono/"
install -m 0644 "$ROOT"/assets/icons/LICENSE-lucide.txt "$ROOTFS/usr/share/miaou/licenses/"
install -m 0644 "$ROOT"/assets/wallpapers/*.jpg "$ROOTFS/usr/share/miaou/wallpapers/"
# --- shell scripts and libraries ---
install -D -m 0755 "$ROOT/src/init" "$ROOTFS/init"
install -D -m 0755 "$ROOT/src/miaoudesk.sh" "$ROOTFS/usr/bin/miaoudesk"
for f in miaou miaou-get miaou-cursors miaou-neko miaou-settings miaou-files miaou-monitor miaou-network miaou-run miaou-apps; do
  install -D -m 0755 "$ROOT/src/usr/bin/$f" "$ROOTFS/usr/bin/$f"
done
for f in package-engine ui cursors neko; do install -D -m 0644 "$ROOT/src/usr/lib/miaou/$f.sh" "$ROOTFS/usr/lib/miaou/$f.sh"; done
ln -sf /usr/bin/miaoudesk "$ROOTFS/bin/miaoudesk"; ln -sf /usr/bin/miaou "$ROOTFS/bin/miaou"; ln -sf /usr/bin/miaou-get "$ROOTFS/bin/miaou-get"
install -D -m 0755 "$ROOT/src/etc/udhcpc.script" "$ROOTFS/usr/share/udhcpc/default.script"
install -D -m 0644 "$ROOT/src/etc/profile" "$ROOTFS/etc/profile"
install -D -m 0644 "$ROOT/src/etc/os-release" "$ROOTFS/etc/os-release"
printf 'miaouos\n' > "$ROOTFS/etc/hostname"
printf '127.0.0.1 localhost miaouos\n' > "$ROOTFS/etc/hosts"
printf 'MiaouOS 0.2 - Le Linux des chats !\n' > "$ROOTFS/etc/issue"
printf 'nameserver 10.0.2.3\n' > "$ROOTFS/etc/resolv.conf"
# --- default accounts (password hash = sha256(salt + password)) and the skeleton copied into every new home
mkdir -p "$ROOTFS/etc/miaou" "$ROOTFS/home" "$ROOTFS/usr/share/miaou/skel"/{Bureau/Documents,Images}
salt_miaou=3f9a1c7d5b2e8a60
hash_miaou="$(printf '%s' "${salt_miaou}miaou" | sha256sum | cut -d' ' -f1)"
printf 'miaou:1000:1000:1:%s:%s:Miaou\ninvite:1001:1001:0:-:-:Invité\n' "$salt_miaou" "$hash_miaou" > "$ROOTFS/etc/miaou/users"
printf 'root:x:0:0:root:/root:/bin/sh\nmiaou:x:1000:1000:Miaou:/home/miaou:/bin/sh\ninvite:x:1001:1001:Invité:/home/invite:/bin/sh\n' > "$ROOTFS/etc/passwd"
printf 'root:x:0:\nmiaou:x:1000:\ninvite:x:1001:\n' > "$ROOTFS/etc/group"
cp "$ROOT/src/etc/Bienvenue.txt" "$ROOTFS/usr/share/miaou/skel/Bureau/Bienvenue.txt"
cp "$ROOT/assets/wallpapers/night.jpg" "$ROOTFS/usr/share/miaou/skel/Bureau/Paysage.jpg"
# --- packages (.miaou archives + catalogue manifests) ---
for pdir in "$ROOT"/packages/*/; do
  name="$(basename "$pdir")"
  tmp="$WORK/pkg-$name"; rm -rf "$tmp"; mkdir -p "$tmp"
  cp "$pdir/manifest" "$tmp/manifest"; cp -a "$pdir/payload" "$tmp/payload"
  chmod -R a+rX "$tmp"; find "$tmp/payload" -path '*/bin/*' -type f -exec chmod 0755 {} +
  tar -czf "$ROOTFS/usr/share/miaou/packages/$name.miaou" -C "$tmp" manifest payload
  cp "$pdir/manifest" "$ROOTFS/var/lib/miaou/repository/$name.manifest"
done
printf 'MiaouOS userland installed into %s\n' "$ROOTFS"
