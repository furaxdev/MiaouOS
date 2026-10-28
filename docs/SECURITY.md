# Sécurité MiaouOS

## Gestion des paquets
Le gestionnaire actuel est expérimental. Les paquets locaux sont des archives tar.gz avec un manifeste et un payload installé dans un répertoire isolé. Ce mécanisme ne constitue pas un bac à sable de sécurité.

Avant un dépôt public, il faut ajouter :
- signatures cryptographiques des index et des paquets;
- vérification des sommes de contrôle;
- HTTPS et rotation des clés;
- résolution et verrouillage des dépendances;
- mises à jour atomiques avec retour arrière;
- protection contre les collisions de fichiers et l'épuisement d'espace;
- tests de fuzzing du format d'archive.

Ne pas exécuter des paquets non fiables comme root. Ne pas déclarer la prise en charge de paquets tiers sûre avant ces étapes.
