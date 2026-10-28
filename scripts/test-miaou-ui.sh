#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec 3<<<"2"
exec 0<&3
. "$ROOT/src/usr/lib/miaou/ui.sh"
miaou_ui_menu "Test MiaouUI" "Premier choix" "Deuxième choix" >/dev/null
[[ "$MIAOU_UI_CHOICE" == "2" ]]
output="$(miaou_ui_progress 100 "Test terminé")"
[[ "$output" == *"100%"* ]]
echo "MiaouUI tests passed."
