# MiaouDesk — le bureau des chats

MiaouDesk est un bureau écrit en C, dessiné directement sur le framebuffer Linux (`/dev/fb0`), sans serveur X ni Wayland.
Rendu anti-aliasé : polices Nunito et JetBrains Mono (`stb_truetype`), icônes **Lucide** (trait fin, une seule taille, teintées avec la couleur du texte ; `tools/gen-icons.py`), fonds d'écran jour/nuit générés par `tools/gen-wallpaper.py`. Style sobre : surfaces gris graphite, un seul accent (ambre par défaut), pas de dégradés ni de lueurs.
Code : `src/desk/` — `gfx.c` (dessin), `desk.c` (fenêtres, panneau, dock, menu, entrées), `app_*.c` (applications).

## Ce qui fonctionne (vérifié dans QEMU par `scripts/qemu_test.py`, sauf mention contraire)

**Session et sécurité**
- Écran de connexion (fond d'écran flouté, horloge, avatars, changement d'utilisateur, secousse et message sur mauvais mot de passe, alerte Verr. Maj.).
- Comptes avec mots de passe stockés en SHA-256 salé (`/etc/miaou/users`), compte d'administration `miaou` (mot de passe `miaou` par défaut, à changer) et compte invité sans mot de passe, effacé à la déconnexion.
- Verrouillage (Super+L), déconnexion, verrouillage automatique après inactivité (réglable), changement d'utilisateur depuis l'écran verrouillé.
- Dossier personnel et réglages par utilisateur ; le terminal d'un compte standard tourne avec ses droits (uid/gid), l'accès aux fichiers du bureau aussi (`setfsuid`).
- Gestion des comptes dans les Paramètres : créer, supprimer, changer le mot de passe.

**Bureau**
- Animations : ouverture, fermeture, réduction, aimantation/agrandissement, lanceur, menus et notifications (désactivables).
- Sélection au rectangle sur le bureau, sélection multiple (Ctrl), clavier (Ctrl+A, flèches, Entrée, Suppr), copier/couper/coller, corbeille, nouveau dossier/fichier, contenu de `~/Bureau` affiché en icônes.
- Bureaux virtuels (4) avec sélecteur dans le panneau, Ctrl+Alt+←/→ ; menu de fenêtre (clic droit sur la barre de titre : réduire, agrandir, toujours au premier plan, déplacer vers un bureau) ; Alt+glisser pour déplacer une fenêtre.
- « Exécuter » (Super+Espace ou Alt+F2) : applications, calculs, commandes dans un terminal, ouverture de chemins, recherche web.
- Centre de contrôle (clic sur l'horloge ou Super+A) : calendrier, silence, animations, thème, clavier, historique des notifications.
- Capture d'écran (Impr. écran) enregistrée dans `~/Images` ; presse-papiers texte partagé ; thème automatique jour/nuit ; fond d'écran personnalisable ; fuseaux horaires ; batterie dans le panneau (si la machine en a une : non testé, pas de batterie dans QEMU).
- Fenêtres : déplacer, redimensionner par les 8 bords, réduire, agrandir (double-clic), aimantation (bords de l'écran ou Super+flèches), Alt+Tab, Alt+F4.
- Panneau de type Plasma (lanceur, icônes d'applications épinglées + applications ouvertes, bureaux, réseau, clavier, thème, horloge), lanceur avec recherche, défilement et boutons Verrouiller/Déconnexion/Console/Redémarrer/Éteindre.
- Thèmes Nuit féline / Jour pastel, 4 couleurs d'accent, échelle d'interface automatique (100, 150 ou 200 %).
- Clavier via codes bruts (`K_MEDIUMRAW`) : AZERTY (défaut) et QWERTY, AltGr. Pointeur : evdev (souris, tablettes absolues, tactile un doigt) avec repli `/dev/input/mice`.

**Applications réelles**
- **Terminal** : pseudo-terminal avec émulation ANSI/VT100, historique, UTF-8, sélection à la souris, copier/coller (Ctrl+Maj+C/V, clic droit), zoom (Ctrl +/−).
- **Fichiers** : multi-sélection, copier/couper/coller, corbeille avec restauration et vidage, fichiers cachés, tri, filtre, propriétés, aperçu texte/image, archives (extraction et création `.tar.gz`), ouverture selon le type.
- **Notes** : éditeur de texte (annuler/rétablir, recherche, indentation automatique, presse-papiers, numéros de ligne).
- **Calculatrice** (priorités, parenthèses, fonctions), **Visionneuse d'images** (zoom, déplacement, images voisines, fond d'écran), **Horloge** (calendrier, chronomètre, minuteur).
- **Navigateur (texte)** : `http`/`https` via BusyBox `wget`, `file://`, liens cliquables, historique, recherche. Pas de JavaScript, de CSS ni d'images ; HTTPS **sans vérification de certificat** (limite de BusyBox).
- **Boutique** : catalogue local, installation / désinstallation / lancement de paquets `.miaou` (`miaou-get`).
- **Moniteur système** : CPU, mémoire, processus (SIGTERM/SIGKILL), disques montés avec usage, journal du noyau.
- **Réseau** : interfaces, IP, MAC, trafic, passerelle, DNS ; DHCP, tests de connexion et DNS.
- **Paramètres** : apparence, affichage, clavier, date et heure (fuseau, synchronisation NTP : non testé hors ligne), comptes, sécurité, raccourcis, système.

## Limites connues
- La résolution est fixée au démarrage par le pilote (`video=Virtual-1:1024x768` sur QEMU). Le changement à chaud via `FBIOPUT_VSCREENINFO` n'est **pas** pris en charge par le pilote `bochs-drm` : MiaouOS ne le simule pas.
- Un seul écran ; pas de multi-écran ni d'accélération GPU. Rendu logiciel.
- Pas de glisser-déposer entre fenêtres, de son, de Bluetooth ni de Wi-Fi.
- Seul le réseau filaire DHCP IPv4 est testé (QEMU `user`). Le matériel réel n'a pas été testé.
- Le navigateur n'est pas un moteur web complet.
- Tout vit dans l'initramfs (RAM) : comptes, mots de passe changés, fichiers et réglages ne survivent pas à un redémarrage tant qu'il n'y a pas de stockage persistant.
- La séparation des utilisateurs concerne les fichiers et le terminal ; le bureau lui-même tourne en root (c'est une session, pas un cloisonnement de sécurité complet).

## Tests
`scripts/test-qemu.sh` démarre le noyau + initramfs dans QEMU, pilote le bureau par QMP (clics via `virtio-tablet`, frappes clavier réelles), prend des captures **PNG réelles** (`screendump`) et vérifie les marqueurs série (`MIAOUDESK_APP_OPEN …`). Aucune image n'est dessinée par le script.
`scripts/test-iso-qemu.sh` fait de même avec l'ISO BIOS/GRUB.
