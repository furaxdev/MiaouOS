# MiaouDesk graphique

## Bureau graphique en cours

Cette branche remplace le lanceur plein écran par un bureau dessiné sur le framebuffer Linux, sans dépendance graphique externe.

- Fond d'écran sombre à motif félin et icônes de bureau.
- Barre des tâches avec lanceur d'applications, fenêtres ouvertes et indicateurs.
- Fenêtres déplaçables par leur barre de titre, superposables, minimisables, maximisables et fermables.
- Fenêtres distinctes pour Fichiers, Navigateur, Miaou Store, Paramètres, Terminal, Réseau, Neko et À propos.
- Lanceur d'applications dans la barre des tâches, navigation souris PS/2 et raccourcis clavier 1–8.
- Le test de démarrage conserve le marqueur série `MIAOUDESK_GUI_OK`.

## Limites honnêtes

C'est une première couche de bureau et de gestion de fenêtres, pas encore un environnement de bureau Linux complet. Les fenêtres Fichiers, Navigateur, Store, Terminal et Réseau affichent actuellement des interfaces de démonstration : elles ne remplacent pas encore des applications pleinement fonctionnelles. Il n'y a pas encore de navigateur Web intégré, de vrai terminal graphique, de compositeur Wayland, de support tactile validé ni de service d'horloge/réseau. Le rendu dépend du framebuffer et le contrôle souris utilise `/dev/input/mice`.

## Suite prévue

- Relier les fenêtres aux véritables outils MiaouOS et au gestionnaire de paquets.
- Ajouter un vrai terminal interactif, une navigation de fichiers avec actions et un navigateur basé sur un moteur existant.
- Améliorer les thèmes, le dock, le curseur, les raccourcis et l'accessibilité.
- Ajouter la gestion evdev et tester le tactile dans QEMU et sur du matériel réel.
