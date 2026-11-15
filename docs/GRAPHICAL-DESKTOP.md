# MiaouDesk graphique

## Fonctions présentes dans cette branche

MiaouDesk est un bureau framebuffer léger, dessiné directement par MiaouOS sans serveur graphique externe.

- Fond d'écran félin et thèmes sombre/clair.
- Icônes de bureau adaptées aux résolutions compactes.
- Barre des tâches avec fenêtres ouvertes et lanceur d'applications.
- Fenêtres déplaçables, redimensionnables depuis leur poignée, superposables, minimisables, maximisables et fermables.
- Bascule de fenêtre avec Alt+Tab et navigation inverse avec Alt+Maj+Tab lorsque le terminal transmet la séquence.
- Curseur de souris dessiné, navigation souris PS/2 et raccourcis clavier 1–8.
- Horloge locale affichée dans la barre des tâches.
- Repli vers la console MiaouOS avec Q.
- Le test de démarrage conserve le marqueur série `MIAOUDESK_GUI_OK`.

## Limites actuelles

C'est une première couche de bureau, pas encore un environnement de bureau Linux complet.

- Le rendu utilise un seul framebuffer `/dev/fb0` : le multi-écran n'est pas encore pris en charge.
- La souris utilise `/dev/input/mice`; evdev et les écrans tactiles ne sont pas encore validés.
- Les fenêtres Fichiers, Navigateur, Store, Terminal et Réseau restent en partie des interfaces de démonstration.
- Il n'y a pas encore de navigateur Web intégré, de terminal graphique interactif, de compositeur Wayland ni de service Bluetooth.
- Le Bluetooth nécessitera l'activation des pilotes noyau adaptés, la gestion des périphériques HCI et l'intégration d'une pile telle que BlueZ avec ses dépendances.

## Prochaines étapes

1. Relier les fenêtres aux véritables outils MiaouOS.
2. Construire un terminal graphique interactif et un gestionnaire de fichiers avec actions.
3. Ajouter une couche d'entrées evdev et tester le tactile.
4. Étudier une architecture DRM/KMS multi-écran avant d'annoncer le support de plusieurs moniteurs.
5. Ajouter une pile Bluetooth complète plutôt qu'une interface factice.
