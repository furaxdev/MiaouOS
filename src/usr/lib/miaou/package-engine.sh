#!/bin/sh
# Shared package engine for miaou and miaou-get.
MIAOU_DB=${MIAOU_DB:-/var/lib/miaou/installed}
MIAOU_CACHE=${MIAOU_CACHE:-/var/cache/miaou}
MIAOU_REPO=${MIAOU_REPO:-/var/lib/miaou/repository}
MIAOU_PACKAGE_ROOT=${MIAOU_PACKAGE_ROOT:-/opt/miaou/packages}

miaou_need_root() {
  [ "$(id -u)" = 0 ] || { echo "miaou: cette opération nécessite root." >&2; return 1; }
}

miaou_init_dirs() {
  mkdir -p "$(dirname "$MIAOU_DB")" "$MIAOU_CACHE" "$MIAOU_REPO"
  touch "$MIAOU_DB"
}

miaou_list() {
  miaou_init_dirs
  if [ ! -s "$MIAOU_DB" ]; then
    echo "Aucun paquet installé via Miaou."
    return 0
  fi
  printf '%-22s %-14s %s\n' "PAQUET" "VERSION" "DESCRIPTION"
  while IFS='|' read -r name version description; do
    [ -n "$name" ] || continue
    printf '%-22s %-14s %s\n' "$name" "$version" "$description"
  done < "$MIAOU_DB"
}

miaou_install() (
  archive=$1
  [ -f "$archive" ] || { echo "Archive introuvable: $archive" >&2; return 1; }
  miaou_need_root || return 1
  miaou_init_dirs
  work=$(mktemp -d "$MIAOU_CACHE/install.XXXXXX") || return 1
  trap 'rm -rf "$work"' EXIT HUP INT TERM
  tar -tzf "$archive" > "$work/paths" 2>/dev/null || { echo "Archive .miaou invalide." >&2; return 1; }
  if grep -E '(^/|(^|/)\.\.(/|$))' "$work/paths" >/dev/null; then
    echo "Archive refusée: chemin non sûr." >&2
    return 1
  fi
  tar -tvzf "$archive" > "$work/listing" 2>/dev/null || { echo "Archive .miaou invalide." >&2; return 1; }
  if ! awk 'substr($0,1,1) == "-" || substr($0,1,1) == "d" { next } { bad=1 } END { exit bad }' "$work/listing"; then
    echo "Archive refusée: seuls les fichiers et dossiers sont acceptés." >&2
    return 1
  fi
  tar -xzf "$archive" -C "$work" || return 1
  [ -f "$work/manifest" ] && [ -d "$work/payload" ] || {
    echo "Format invalide: manifest et payload/ requis." >&2
    return 1
  }
  name=$(sed -n 's/^name=//p' "$work/manifest" | head -n 1)
  version=$(sed -n 's/^version=//p' "$work/manifest" | head -n 1)
  description=$(sed -n 's/^description=//p' "$work/manifest" | head -n 1)
  case "$name" in ''|*[!A-Za-z0-9._+-]*) echo "Nom de paquet invalide." >&2; return 1;; esac
  case "$version" in ''|*[!A-Za-z0-9._+-]*) echo "Version invalide." >&2; return 1;; esac
  [ -n "$description" ] || description="Sans description"
  case "$description" in *'|'*) echo "Description invalide: le caractère | est réservé." >&2; return 1;; esac
  if awk -F '|' -v n="$name" '$1 == n { found=1 } END { exit !found }' "$MIAOU_DB"; then
    echo "Le paquet $name est déjà enregistré. La mise à jour sera prise en charge dans une prochaine version." >&2
    return 1
  fi
  if find "$work/payload" -type l | grep . >/dev/null 2>&1; then
    echo "Archive refusée: les liens symboliques ne sont pas acceptés." >&2
    return 1
  fi
  (cd "$work/payload" && find . -type f) > "$work/files"
  target="$MIAOU_PACKAGE_ROOT/$name"
  [ ! -e "$target" ] || { echo "Dossier de paquet déjà présent: $target" >&2; return 1; }
  while IFS= read -r rel; do
    rel=${rel#./}
    case "$rel" in ''|/*|..|../*|*/../*|*/..) echo "Chemin de fichier refusé: $rel" >&2; return 1;; esac
  done < "$work/files"
  mkdir -p "$target"
  while IFS= read -r rel; do
    rel=${rel#./}
    [ -n "$rel" ] || continue
    mkdir -p "$target/$(dirname "$rel")"
    cp -p "$work/payload/$rel" "$target/$rel" || return 1
  done < "$work/files"
  printf '%s|%s|%s\n' "$name" "$version" "$description" >> "$MIAOU_DB"
  cp "$work/files" "$(dirname "$MIAOU_DB")/$name.files"
  echo "Paquet installé: $name $version"
)

miaou_remove() {
  name=$1
  miaou_need_root || return 1
  miaou_init_dirs
  case "$name" in ''|*[!A-Za-z0-9._+-]*) echo "Nom de paquet invalide." >&2; return 1;; esac
  awk -F '|' -v n="$name" '$1 == n { found=1 } END { exit !found }' "$MIAOU_DB" || {
    echo "Paquet non enregistré: $name" >&2
    return 1
  }
  target="$MIAOU_PACKAGE_ROOT/$name"
  rm -rf "$target"
  rm -f "$(dirname "$MIAOU_DB")/$name.files"
  awk -F '|' -v n="$name" '$1 != n' "$MIAOU_DB" > "$MIAOU_DB.tmp"
  mv "$MIAOU_DB.tmp" "$MIAOU_DB"
  echo "Paquet supprimé de son répertoire isolé: $name"
}

miaou_search() {
  query=$1
  found=0
  for manifest in "$MIAOU_REPO"/*.manifest; do
    [ -f "$manifest" ] || continue
    if grep -i -q -e "$query" "$manifest"; then
      sed -n -e 's/^name=/Paquet: /p' -e 's/^version=/Version: /p' -e 's/^description=/Description: /p' "$manifest"
      printf '\n'
      found=1
    fi
  done
  [ "$found" = 1 ] || echo "Aucun résultat dans le dépôt local. Le dépôt réseau signé n'est pas encore disponible."
}

miaou_help() {
  cat <<'HELP'
MiaouOS — gestionnaire de paquets
Usage: miaou-get <commande> [arguments]
  list                 Afficher les paquets enregistrés
  install fichier.miaou Installer une archive locale
  remove nom           Supprimer un paquet isolé
  search texte         Chercher dans le catalogue local
  help                 Afficher cette aide

Format .miaou v0: archive tar.gz avec manifest (name, version, description)
et répertoire payload/. Les paquets doivent provenir d'une source de confiance.
HELP
}
