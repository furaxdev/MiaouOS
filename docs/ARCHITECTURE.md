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

Le dépôt possède un pipeline de compilation du noyau Linux et de BusyBox, un initramfs et un prototype de console MiaouDesk. Le bureau graphique, le toolkit MiaouUI, le gestionnaire de paquets, le catalogue de curseurs, Miaou Neko et la compatibilité Windows restent des objectifs futurs.
