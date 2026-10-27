# MiaouOS

**MiaouOS** est une distribution Linux indépendante construite depuis les sources, sans base Ubuntu, Debian, Arch ou autre distribution préexistante.

## Architecture de MiaouOS 0.1

- Noyau Linux compilé depuis les sources amont.
- BusyBox compilé statiquement depuis ses sources pour le système minimal.
- Notre propre `/init` et notre propre espace utilisateur de démarrage.
- `MiaouDesk`, prototype de bureau textuel personnalisé (console Linux) pour valider le cycle de démarrage et l'identité du système.
- GitHub Actions : compilation, test de démarrage QEMU, vérification du marqueur de démarrage, capture PNG de l'écran de la VM et publication des artefacts.

Ce premier jalon n'est pas encore un desktop graphique complet : les fenêtres graphiques, la souris, le compositeur Wayland et le gestionnaire de paquets maison seront des étapes ultérieures.

## Construire

Sur un runner Ubuntu récent ou une machine Linux, installez `build-essential bc bison flex libelf-dev libssl-dev cpio xz-utils wget qemu-system-x86 xvfb python3` puis lancez :

```sh
./scripts/build.sh
./scripts/test-qemu.sh
```

Le build télécharge des sources amont épinglées (Linux 6.12.30 et BusyBox 1.37.0), compile un noyau x86_64 et crée un initramfs autonome. Aucun système de fichiers d'une distribution existante n'est utilisé comme rootfs.

## CI/CD

Le workflow `.github/workflows/miaouos.yml` se lance sur push, pull request et manuellement. Les résultats de compilation, journaux et captures sont publiés comme artefacts. La compilation peut nécessiter des ajustements selon les contraintes des runners GitHub.
