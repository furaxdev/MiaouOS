#!/bin/sh
# Shared package engine for miaou and miaou-get.
MIAOU_DB=${MIAOU_DB:-/var/lib/miaou/installed}
MIAOU_CACHE=${MIAOU_CACHE:-/var/cache/miaou}
MIAOU_REPO=${MIAOU_REPO:-/var/lib/miaou/repository}

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

miaou_install() {
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
  if grep -F "|$name|" "$MIAOU_DB" >/dev/null 2>&1; then
    echo "Le paquet $name est déjà enregistré. La mise à jour sera prise en charge dans une prochaine version." >&2
    return 1
  fi
  (cd "$work/payload" && find . -type f -o -type l) > "$work/files"
  while IFS= read -r rel; do
    rel=${rel#./}
    case "$rel" in ''|/*|..|../*|*/../*|*/..) echo "Chemin de fichier refusé: $rel" >&2; return 1;; esac
  done < "$work/files"
  while IFS= read -r rel; do
    rel=${rel#./}
    [ -n "$rel" ] || continue
    mkdir -p "/$(dirname "$rel")"
    cp -a "$work/payload/$rel" "/$rel" || return 1
  done < "$work/files"
  printf '%s|%s|%s\n' "$name" "$version" "$description" >> "$MIAOU_DB"
  cp "$work/files" "$(dirname "$MIAOU_DB")/$name.files"
  echo "Paquet installé: $name $version"
}

miaou_remove() {
  name=$1
  miaou_need_root || return 1
  miaou_init_dirs
  case "$name" in ''|*[!A-Za-z0-9._+-]*) echo "Nom de paquet invalide." >&2; return 1;; esac
  grep -F "|$name|" "$MIAOU_DB" >/dev/null 2>&1 || {
    echo "Paquet non enregistré: $name" >&2
    return 1
  }
  # Never delete installed files automatically until ownership/conflict tracking is stronger.
  echo "Désinstallation bloquée pour sécurité: le suivi des fichiers partagés doit être renforcé."
  echo "Le paquet reste installé. Aucune donnée n'a été supprimée."
  return 2
}

miaou_search() {
  query=$1
  found=0
  for manifest in "$MIAOU_REPO"/*.manifest; do
    [ -f "$manifest" ] || continue
    if grep -i -q "$query" "$manifest"; then
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
  remove nom           Désinstaller (désactivation de sécurité pour le moment)
  search texte         Chercher dans le catalogue local
  help                 Afficher cette aide

Format .miaou v0: archive tar.gz avec manifest (name, version, description)
et répertoire payload/. Les paquets doivent provenir d'une source de confiance.
HELP
}
