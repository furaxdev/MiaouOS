# MiaouOS

**MiaouOS — Le Linux des chats !**

MiaouOS est une distribution Linux indépendante construite depuis les sources amont, sans base Ubuntu, Debian, Arch ou autre distribution existante.

## Ce qui est présent dans cette branche

- Noyau Linux 6.12.30 et BusyBox 1.37.0 compilés depuis les sources.
- Initramfs et processus `/init` propres à MiaouOS.
- MiaouDesk : lanceur graphique framebuffer (si le noyau fournit `/dev/fb0`) avec clics souris et raccourcis clavier; repli automatique vers la console.
- Lanceur console `miaou-apps`.
- Outils console `miaou-files`, `miaou-monitor`, `miaou-network`.
- Miaou Store (`miaou`) et interface scriptable `miaou-get`, avec moteur partagé et format local `.miaou`.
- Un paquet d'exemple Miaou Welcome livré dans l'image.
- MiaouUI : primitives TUI shell.
- Miaou Cursors : détection et sélection de thèmes installés.
- Miaou Neko : réglage activé/désactivé et animation console.
- Lanceur Windows optionnel `miaou-run`, qui utilise Wine uniquement si Wine est installé.

## État réel — à lire avant test

**Cette branche n'est pas encore un OS de bureau complet.** Le bureau graphique actuel est un lanceur framebuffer à plein écran, pas encore une session Wayland ni un gestionnaire de fenêtres avec fenêtres redimensionnables. L'installateur, le dépôt réseau signé, les dépendances du gestionnaire de paquets et la compatibilité Windows validée restent à faire. Miaou Neko est une animation console, pas encore un compagnon flottant sur un bureau graphique. Les applications installées via le prototype sont isolées sous `/opt/miaou/packages/` et ne sont pas ajoutées au PATH.

Les cases de la feuille de route indiquent le code présent, pas nécessairement des tests d'exécution réussis. Le CI doit confirmer la compilation et le démarrage QEMU avant de considérer l'image vérifiée.

## Commandes utiles

- `miaou-apps` : lanceur d'applications console, accessible aussi depuis MiaouDesk graphique.
- `miaou-files` : gestionnaire de fichiers console.
- `miaou-monitor` : aperçu mémoire et processus.
- `miaou-network` : état des interfaces, routes et test ping.
- `miaou` : ouvre Miaou Store.
- `miaou-get help`, `list`, `catalog`, `search TEXTE`, `verify FICHIER.miaou`.
- `miaou-get install /usr/share/miaou/packages/miaou-welcome.miaou`.
- `miaou-get remove miaou-welcome`.
- `miaou-neko on|off|status|play`.
- `miaou-cursors list`.
- `miaou-run application.exe` : nécessite Wine installé séparément.

Le format `.miaou` actuel est une archive tar.gz contenant `manifest` et `payload/`. Il n'inclut pas encore de signature cryptographique.

## Construire

Sur Ubuntu récent, installez :

```sh
sudo apt-get install build-essential bc bison flex libelf-dev libssl-dev cpio xz-utils wget qemu-system-x86 xvfb python3 grub-common grub-pc-bin grub-efi-amd64-bin xorriso mtools
bash ./scripts/build.sh
xvfb-run -a bash ./scripts/test-qemu.sh
bash ./scripts/test-iso-qemu.sh
```

Le script construit un noyau x86_64 et un initramfs autonome. Une ISO amorçable GRUB est générée dans `build/miaouos.iso`; le CI tente aussi de la démarrer dans QEMU. Un installateur sur disque reste à développer.

## CI

Le workflow `.github/workflows/miaouos.yml` compile, teste les scripts, lance QEMU et publie les artefacts lorsque les jobs passent. Résultats : https://github.com/furaxdev/MiaouOS/actions
