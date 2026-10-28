#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export MIAOU_DB="$TMP/state/installed"
export MIAOU_CACHE="$TMP/cache"
export MIAOU_REPO="$TMP/repository"
export MIAOU_PACKAGE_ROOT="$TMP/packages"
. "$ROOT/src/usr/lib/miaou/package-engine.sh"

mkdir -p "$TMP/pkg/payload/usr/share/miaou-demo" "$TMP/pkg/payload/usr/bin"
cat > "$TMP/pkg/manifest" <<'MANIFEST'
name=miaou-demo
version=0.1.0
description=Paquet de test MiaouOS
MANIFEST
printf 'MiaouOS package test\n' > "$TMP/pkg/payload/usr/share/miaou-demo/hello.txt"
tar -czf "$TMP/miaou-demo.miaou" -C "$TMP/pkg" manifest payload
mkdir -p "$MIAOU_REPO"
cp "$TMP/pkg/manifest" "$MIAOU_REPO/miaou-demo.manifest"
miaou_verify "$TMP/miaou-demo.miaou"
miaou_catalog | grep -q "miaou-demo"
miaou_search "MiaouOS" | grep -q "miaou-demo"

miaou_install "$TMP/miaou-demo.miaou"
test -f "$MIAOU_PACKAGE_ROOT/miaou-demo/usr/share/miaou-demo/hello.txt"
grep -q '^miaou-demo|0.1.0|' "$MIAOU_DB"
miaou_list | grep -q 'miaou-demo'
miaou_remove miaou-demo
test ! -e "$MIAOU_PACKAGE_ROOT/miaou-demo"
! grep -q '^miaou-demo|' "$MIAOU_DB"

mkdir -p "$TMP/bad/payload"
ln -s /tmp "$TMP/bad/payload/escape"
cp "$TMP/pkg/manifest" "$TMP/bad/manifest"
tar -czf "$TMP/bad.miaou" -C "$TMP/bad" manifest payload
if miaou_install "$TMP/bad.miaou"; then
  echo "Symlink package was incorrectly accepted" >&2
  exit 1
fi

echo "Miaou package engine tests passed."
