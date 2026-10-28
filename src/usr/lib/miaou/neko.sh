#!/bin/sh
MIAOU_NEKO_CONFIG=${MIAOU_NEKO_CONFIG:-${HOME:-/root}/.config/miaou/neko.conf}
miaou_neko_status() { if [ -f "$MIAOU_NEKO_CONFIG" ] && grep -q '^enabled=true$' "$MIAOU_NEKO_CONFIG"; then echo "Miaou Neko: activé"; else echo "Miaou Neko: désactivé"; fi; }
miaou_neko_set() {
 case "$1" in on|enable|true) value=true;; off|disable|false) value=false;; *) echo "Usage: miaou-neko on|off|status" >&2; return 2;; esac
 mkdir -p "$(dirname "$MIAOU_NEKO_CONFIG")"
 printf 'enabled=%s\n' "$value" > "$MIAOU_NEKO_CONFIG"
 echo "Miaou Neko: $([ "$value" = true ] && echo activé || echo désactivé)"
}
