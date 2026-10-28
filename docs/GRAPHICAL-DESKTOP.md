# MiaouDesk graphique

Le système actuel n'embarque pas de serveur graphique ni de compositeur. Le menu console n'est pas un environnement graphique, et les outils de curseurs/compagnon actuels ne créent pas de fenêtres.

## Architecture visée
- Session graphique Wayland avec un compositeur maintenu en amont.
- Panneau et lanceur MiaouDesk, paramètres et notifications.
- MiaouUI partagé entre TUI et composants graphiques.
- Prise en charge clavier, souris, tactile, lecteurs d'écran et faible consommation.
- Miaou Neko dessiné comme sprite animé dans la session, avec option de désactivation.
- Thèmes de curseurs installés par utilisateur.

## Intégration
Une image graphique doit compiler et intégrer le compositeur, les bibliothèques, les pilotes et les applications, gérer les sessions et permissions, et tester les pilotes réels. Le simple ajout d'un script shell ne suffit pas à fournir un bureau graphique.
