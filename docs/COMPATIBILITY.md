# Compatibilité des applications Windows

La compatibilité Windows est un objectif de MiaouOS, pas une capacité garantie dans la version actuelle.

## Approche
- Sur une édition Linux x86_64 graphique, étudier Wine comme couche de compatibilité.
- Ne pas confondre Wine avec ReactOS : ReactOS est un système d'exploitation distinct.
- Proposer un lanceur de compatibilité uniquement si Wine et ses bibliothèques sont réellement installés.
- Afficher les limites par application; les pilotes noyau Windows et certains logiciels protégés ne seront pas compatibles.
- Garder cette fonctionnalité optionnelle, désactivable et séparée du cœur du système.

## État
Aucun moteur Windows n'est intégré au système minimal actuel. Les scripts de MiaouOS ne doivent jamais prétendre qu'un fichier .exe fonctionnera tant que Wine n'est pas disponible et testé.
