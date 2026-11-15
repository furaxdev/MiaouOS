# MiaouDesk — le bureau des chats

MiaouDesk est un bureau écrit en C, dessiné directement sur le framebuffer Linux (`/dev/fb0`), sans serveur X ni Wayland.
Rendu anti-aliasé : polices Nunito et JetBrains Mono (`stb_truetype`), icônes **Lucide** (trait fin, une seule taille, teintées avec la couleur du texte ; `tools/gen-icons.py`), fonds d'écran jour/nuit générés par `tools/gen-wallpaper.py`. Style sobre : surfaces gris graphite, un seul accent (ambre par défaut), pas de dégradés ni de lueurs.
Code : `src/desk/` — `gfx.c` (dessin), `desk.c` (fenêtres, panneau, dock, menu, entrées), `app_*.c` (applications).

## Ce qui fonctionne (vérifié dans QEMU par `scripts/qemu_test.py`)

**Bureau**
- Fenêtres : déplacer, redimensionner par les 8 bords, réduire, agrandir (double-clic sur la barre de titre), fermer, pile Z avec focus.
- Aimantation : glisser une fenêtre au bord gauche/droit (moitié d'écran) ou en haut (plein écran) ; Super+←/→/↑/↓.
- Disposition de type Plasma : panneau en bas (lanceur, icônes des applications avec indicateur d'ouverture, réseau réel, disposition clavier, thème, horloge/date, bouton « afficher le bureau »), lanceur d'applications avec recherche et boutons Console/Redémarrer/Éteindre, icônes sur le bureau (double-clic), menu contextuel (clic droit), notifications.
- Raccourcis : Super = menu, Alt+Tab (Maj = inverse), Alt+F4, Ctrl+Alt+T, Super+T/E/B/D.
- Thèmes Nuit féline / Jour pastel, 4 couleurs d'accent, échelle d'interface automatique selon la taille d'écran (100, 150 ou 200 %).
- Clavier via codes bruts (`K_MEDIUMRAW`) : dispositions AZERTY (par défaut) et QWERTY, modifiables à chaud ; AltGr pris en charge.
- Pointeur : evdev (souris, tablettes absolues type `virtio-tablet`, écrans tactiles à un doigt) avec repli sur `/dev/input/mice`.
- Diagnostic : un crash du bureau écrit `MIAOUDESK_CRASH` + une trace sur la console série.

**Applications réelles**
- **Terminal** : vrai pseudo-terminal (`/bin/sh` BusyBox), émulation ANSI/VT100 (couleurs, curseur, effacement), historique de défilement, UTF-8.
- **Fichiers** : navigation, aperçu texte et images (PNG/JPEG), création de dossier/fichier, renommage, suppression.
- **Navigateur (texte)** : pages `http`/`https` via BusyBox `wget` (asynchrone), `file://`, liens cliquables, historique, recherche. Pas de JavaScript, de CSS ni d'images ; HTTPS **sans vérification de certificat** (limite de BusyBox).
- **Boutique** : catalogue local, installation / désinstallation / lancement de paquets `.miaou` avec `miaou-get` (sortie en direct).
- **Moniteur système** : CPU, mémoire, charge, liste des processus triable, envoi de SIGTERM/SIGKILL.
- **Réseau** : interfaces, IP, MAC, trafic, passerelle, DNS ; DHCP, test de connexion et DNS.
- **Paramètres** : thème, accent, échelle, clavier, informations système (réglages sauvegardés dans `/etc/miaou/desktop.conf`).

## Limites connues
- La résolution est fixée au démarrage par le pilote (`video=Virtual-1:1024x768` sur QEMU). Le changement à chaud via `FBIOPUT_VSCREENINFO` n'est **pas** pris en charge par le pilote `bochs-drm` : MiaouOS ne le simule pas.
- Un seul écran ; pas de multi-écran ni d'accélération GPU. Rendu logiciel.
- Pas de presse-papiers, de glisser-déposer, de son, de Bluetooth ni de Wi-Fi.
- Seul le réseau filaire DHCP IPv4 est testé (QEMU `user`). Le matériel réel n'a pas été testé.
- Le navigateur n'est pas un moteur web complet.
- `/etc/miaou/desktop.conf` vit dans l'initramfs (RAM) : les réglages ne survivent pas à un redémarrage tant qu'il n'y a pas de stockage persistant.

## Tests
`scripts/test-qemu.sh` démarre le noyau + initramfs dans QEMU, pilote le bureau par QMP (clics via `virtio-tablet`, frappes clavier réelles), prend des captures **PNG réelles** (`screendump`) et vérifie les marqueurs série (`MIAOUDESK_APP_OPEN …`). Aucune image n'est dessinée par le script.
`scripts/test-iso-qemu.sh` fait de même avec l'ISO BIOS/GRUB.
