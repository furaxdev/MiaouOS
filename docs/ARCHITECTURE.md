# Architecture proposée de MiaouOS

## Vision

MiaouOS est une distribution indépendante, construite depuis les sources amont. Le noyau Linux et les composants tiers restent attribués à leurs projets respectifs. L'identité propre de MiaouOS vient de son intégration, de ses outils, de son environnement de bureau et de sa documentation.

## Couches

1. **MiaouCore** — noyau Linux, initramfs, BusyBox, initialisation et services système.
2. **MiaouUI** — toolkit TUI indépendant des applications, d'abord clavier, puis souris quand le terminal le permet.
3. **Miaou Package Engine** — moteur unique de résolution, téléchargement, vérification et installation.
4. **Interfaces de paquets** — miaou pour l'usage interactif ; miaou-get pour scripts et automatisation ; Miaou Store comme interface TUI.
5. **MiaouDesk** — futur environnement graphique avec panneau, lanceur, paramètres et gestionnaire de sessions.
6. **Personnalisation** — Miaou Cursors, thèmes et Miaou Neko, activable dans les paramètres.
7. **Compatibilité** — intégration éventuelle de technologies existantes comme Wine après validation ; ReactOS est un projet de système compatible Windows, pas un simple composant à greffer dans le système.

## Principes de conception

- Un seul moteur de paquets derrière les interfaces interactives et scriptables.
- Le clavier doit rester une méthode d'accès complète ; la souris est une amélioration, pas une exigence du TUI.
- Paquets versionnés, métadonnées explicites, sommes de contrôle et signatures avant de proposer un dépôt public.
- Fonctionnalités isolées, tests automatiques et capacité de revenir sur une mise à jour.
- Aucun élément de la feuille de route n'est considéré comme implémenté avant qu'un test ou une démonstration reproductible le confirme.

## État actuel

- **MiaouCore :** compilation du noyau Linux et de BusyBox, initramfs et lancement de MiaouDesk.
- **Miaou Package Engine :** prototype shell commun aux deux interfaces. Le format local `.miaou` est une archive tar.gz contenant `manifest` et `payload/`; les fichiers sont installés dans un répertoire isolé sous `/opt/miaou/packages/`.
- **miaou-get :** commandes de base pour lister, installer une archive locale, supprimer un paquet isolé et rechercher dans un catalogue local.
- **miaou / Miaou Store :** menu texte initial accessible depuis MiaouDesk.
- **CI :** compilation et test QEMU, plus tests shell et test du moteur de paquets.

Ce gestionnaire est encore expérimental : il ne dispose pas de dépôt réseau signé, de résolution de dépendances, de mise à jour atomique, ni d'intégration des exécutables installés dans le PATH. N'installez que des archives de confiance. Le bureau graphique, MiaouUI comme bibliothèque dédiée, le catalogue de curseurs, Miaou Neko et la compatibilité Windows restent à construire.
