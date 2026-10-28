# MiaouDesk graphique

## Implémenté dans cette branche
- Lanceur plein écran dessiné directement sur le framebuffer Linux, sans bibliothèque graphique externe.
- Cartes pour Applications, Store, Fichiers, Paramètres, Réseau, Neko, Terminal et retour console.
- Contrôle au clavier (touches 1–8, Q pour revenir à la console) et clic gauche via `/dev/input/mice` si ce périphérique existe.
- Les outils console s'exécutent en mode texte; le framebuffer est ensuite restauré.
- Si aucun `/dev/fb0` n'est disponible, `/init` revient au menu console.

## Limites actuelles
Ce lanceur n'est pas un bureau Wayland : il n'a pas de fenêtres multiples, de compositeur, de dock permanent, de notifications, de gestion de sessions, de tactile testé ni de support lecteurs d'écran. Le contrôle souris dépend du périphérique PS/2 `/dev/input/mice`; d'autres dispositifs peuvent nécessiter une prise en charge evdev plus complète.

## Architecture à compléter
- Session Wayland avec un compositeur maintenu.
- Fenêtres, panneau, dock, lanceur, notifications et paramètres graphiques.
- MiaouUI partagé entre TUI et composants graphiques.
- Miaou Neko en sprite animé dans la session, désactivable.
- Thèmes de curseurs avec aperçus réels.
- Accessibilité, tactile et tests matériels.
