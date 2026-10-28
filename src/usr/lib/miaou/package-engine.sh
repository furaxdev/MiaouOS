#!/bin/sh
MIAOU_DB=${MIAOU_DB:-/var/lib/miaou/installed}
MIAOU_CACHE=${MIAOU_CACHE:-/var/cache/miaou}
MIAOU_REPO=${MIAOU_REPO:-/var/lib/miaou/repository}
MIAOU_PACKAGE_ROOT=${MIAOU_PACKAGE_ROOT:-/opt/miaou/packages}
miaou_init_dirs() { mkdir -p "$(dirname "$MIAOU_DB")" "$MIAOU_CACHE" "$MIAOU_REPO" "$MIAOU_PACKAGE_ROOT"; touch "$MIAOU_DB"; }
miaou_need_root() { [ "$(id -u)" = 0 ] || { echo "Cette opération nécessite root." >&2; return 1; }; }
miaou_list() { miaou_init_dirs; if [ ! -s "$MIAOU_DB" ]; then echo "Aucun paquet installé."; return; fi; printf '%-24s %-12s %s\n' PAQUET VERSION DESCRIPTION; while IFS='|' read -r n v d; do [ -n "$n" ] && printf '%-24s %-12s %s\n' "$n" "$v" "$d"; done < "$MIAOU_DB"; }
miaou_install() (
 archive=$1; [ -f "$archive" ] || { echo "Archive introuvable." >&2; exit 1; }; miaou_need_root || exit 1; miaou_init_dirs
 work=$(mktemp -d "$MIAOU_CACHE/install.XXXXXX") || exit 1; trap 'rm -rf "$work"' EXIT HUP INT TERM
 tar -tzf "$archive" > "$work/paths" 2>/dev/null || { echo "Archive invalide." >&2; exit 1; }
 if grep -E '(^/|(^|/)\.\.(/|$))' "$work/paths" >/dev/null; then echo "Chemin dangereux." >&2; exit 1; fi
 tar -tvzf "$archive" > "$work/listing" 2>/dev/null || exit 1
 awk 'substr($0,1,1)=="-" || substr($0,1,1)=="d" {next} {bad=1} END {exit bad}' "$work/listing" || { echo "Seuls fichiers et dossiers acceptés." >&2; exit 1; }
 tar -xzf "$archive" -C "$work" || exit 1
 [ -f "$work/manifest" ] && [ -d "$work/payload" ] || { echo "manifest et payload/ requis." >&2; exit 1; }
 name=$(sed -n 's/^name=//p' "$work/manifest" | head -n 1); version=$(sed -n 's/^version=//p' "$work/manifest" | head -n 1); description=$(sed -n 's/^description=//p' "$work/manifest" | head -n 1)
 case "$name" in ''|*[!A-Za-z0-9._+-]*) echo "Nom invalide." >&2; exit 1;; esac
 case "$version" in ''|*[!A-Za-z0-9._+-]*) echo "Version invalide." >&2; exit 1;; esac
 case "$description" in *'|'*) echo "Description invalide." >&2; exit 1;; esac
 [ -n "$description" ] || description="Sans description"
 if awk -F '|' -v n="$name" '$1==n {found=1} END {exit !found}' "$MIAOU_DB"; then echo "Paquet déjà installé." >&2; exit 1; fi
 if find "$work/payload" -type l | grep . >/dev/null 2>&1; then echo "Liens symboliques refusés." >&2; exit 1; fi
 (cd "$work/payload" && find . -type f) > "$work/files"; target="$MIAOU_PACKAGE_ROOT/$name"; [ ! -e "$target" ] || { echo "Cible déjà présente." >&2; exit 1; }
 mkdir -p "$target" || exit 1
 while IFS= read -r rel; do rel=${rel#./}; case "$rel" in ''|/*|..|../*|*/../*|*/..) echo "Chemin invalide." >&2; exit 1;; esac; mkdir -p "$target/$(dirname "$rel")"; cp -p "$work/payload/$rel" "$target/$rel" || exit 1; done < "$work/files"
 printf '%s|%s|%s\n' "$name" "$version" "$description" >> "$MIAOU_DB"; echo "Installé: $name $version dans $target"
)
miaou_remove() { name=$1; miaou_need_root || return 1; miaou_init_dirs; case "$name" in ''|*[!A-Za-z0-9._+-]*) echo "Nom invalide." >&2; return 1;; esac; awk -F '|' -v n="$name" '$1==n {found=1} END {exit !found}' "$MIAOU_DB" || { echo "Paquet inconnu." >&2; return 1; }; target="$MIAOU_PACKAGE_ROOT/$name"; case "$target" in "$MIAOU_PACKAGE_ROOT/"*) rm -rf -- "$target" || return 1;; *) return 1;; esac; awk -F '|' -v n="$name" '$1!=n' "$MIAOU_DB" > "$MIAOU_DB.tmp" && mv "$MIAOU_DB.tmp" "$MIAOU_DB"; echo "Supprimé: $name"; }
miaou_catalog() { miaou_init_dirs; found=0; for m in "$MIAOU_REPO"/*.manifest; do [ -f "$m" ] || continue; echo "----------------------------------------"; sed -n -e 's/^name=/Paquet : /p' -e 's/^version=/Version : /p' -e 's/^description=/Description : /p' "$m"; found=1; done; [ "$found" = 1 ] || echo "Le catalogue local est vide."; }
miaou_search() { query=$1; found=0; for m in "$MIAOU_REPO"/*.manifest; do [ -f "$m" ] || continue; if grep -i -q -e "$query" "$m"; then sed -n -e 's/^name=/Paquet: /p' -e 's/^version=/Version: /p' -e 's/^description=/Description: /p' "$m"; printf '\n'; found=1; fi; done; [ "$found" = 1 ] || echo "Aucun résultat dans le catalogue local."; }
miaou_help() { cat <<'HELP'
miaou-get list | catalog | search TEXTE | install FICHIER.miaou | remove NOM | verify FICHIER.miaou
Format local tar.gz : manifest + payload/. Les paquets restent isolés dans /opt/miaou/packages.
Les signatures et le dépôt réseau ne sont pas encore disponibles.
HELP
}
miaou_verify() { archive=$1; [ -f "$archive" ] || return 1; tar -tzf "$archive" >/dev/null 2>&1 || { echo "Archive invalide"; return 1; }; tar -xOzf "$archive" manifest 2>/dev/null | grep -Eq '^name=[A-Za-z0-9._+-]+$' || { echo "Manifest invalide"; return 1; }; echo "Archive lisible et nom valide (aucune signature vérifiée)."; }
