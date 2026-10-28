#!/bin/sh
# MiaouUI: small terminal UI primitives for MiaouOS.
MIAOU_UI_ACCENT='\033[1;35m'
MIAOU_UI_RESET='\033[0m'

miaou_ui_clear() {
  printf '\033[2J\033[H'
}

miaou_ui_title() {
  printf '%b\n' "$MIAOU_UI_ACCENT"
  printf '  %s\n' "$1"
  printf '%b\n' "$MIAOU_UI_RESET"
  printf '  ────────────────────────────────────────────────────\n\n'
}

miaou_ui_button() {
  printf '  [ %s ]' "$1"
}

miaou_ui_prompt() {
  printf '%b' "$1"
  IFS= read -r MIAOU_UI_REPLY || return 1
}

miaou_ui_menu() {
  title=$1
  shift
  miaou_ui_title "$title"
  index=1
  for item in "$@"; do
    printf '  [%s] %s\n' "$index" "$item"
    index=$((index + 1))
  done
  printf '  [0] Quitter\n\n'
  miaou_ui_prompt '  Votre choix : ' || return 1
  MIAOU_UI_CHOICE=$MIAOU_UI_REPLY
}

miaou_ui_progress() {
  percent=$1
  message=$2
  case "$percent" in ''|*[!0-9]*) percent=0;; esac
  [ "$percent" -le 100 ] || percent=100
  filled=$((percent / 5))
  bar=''
  index=0
  while [ "$index" -lt "$filled" ]; do
    bar="$bar#"
    index=$((index + 1))
  done
  while [ "$index" -lt 20 ]; do
    bar="$bar-"
    index=$((index + 1))
  done
  printf '\r  [%s] %3s%% %s' "$bar" "$percent" "$message"
  [ "$percent" -eq 100 ] && printf '\n'
}

miaou_ui_notice() {
  printf '\n  %s\n\n' "$1"
  miaou_ui_prompt '  Entrée pour continuer...' || true
}
