#!/bin/sh
MIAOU_CURSOR_DIR=${MIAOU_CURSOR_DIR:-/usr/share/icons}
miaou_cursors_list() {
  echo "Miaou Cursors — thèmes disponibles"
  found=0
  for d in "$MIAOU_CURSOR_DIR"/*; do
    [ -d "$d/cursors" ] || continue
    printf '  %s\n' "${d##*/}"
    found=1
  done
  [ "$found" = 1 ] || echo "Aucun thème de curseur installé."
}
miaou_cursors_preview() {
  theme=$1
  [ -d "$MIAOU_CURSOR_DIR/$theme/cursors" ] || { echo "Thème inconnu: $theme" >&2; return 1; }
  echo "Thème détecté: $theme"
  echo "L'aperçu graphique sera disponible avec MiaouDesk graphique."
}
miaou_cursors_set() {
  theme=$1
  [ -d "$MIAOU_CURSOR_DIR/$theme/cursors" ] || { echo "Thème inconnu: $theme" >&2; return 1; }
  mkdir -p "${HOME:-/root}/.icons"
  ln -sfn "$MIAOU_CURSOR_DIR/$theme" "${HOME:-/root}/.icons/default"
  echo "Thème sélectionné pour les applications compatibles: $theme"
}
