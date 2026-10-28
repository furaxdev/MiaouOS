# Guide utilisateur MiaouOS (prototype)

## Démarrage
Le prototype actuel démarre en console. MiaouDesk affiche un menu de commandes; ce n'est pas encore un bureau graphique.

## Paquets
- `miaou-get help` : aide.
- `miaou-get list` : liste les paquets enregistrés.
- `miaou-get search TEXTE` : recherche dans le catalogue local.
- `miaou-get verify FICHIER.miaou` : contrôle la lisibilité du manifeste; ne valide aucune signature.
- `miaou-get install FICHIER.miaou` : installe dans `/opt/miaou/packages/NOM/`.
- `miaou-get remove NOM` : supprime le répertoire isolé du paquet.
- `miaou` : ouvre le menu Miaou Store.

Les exécutables installés ne sont pas ajoutés au PATH. Les dépôts réseau et dépendances ne sont pas encore disponibles.

## Miaou Neko
- `miaou-neko status`
- `miaou-neko on`
- `miaou-neko off`

Cette version stocke le réglage, mais ne lance pas encore de compagnon animé.

## Curseurs
- `miaou-cursors list`
- `miaou-cursors preview THEME`
- `miaou-cursors set THEME`

La prévisualisation graphique requiert le futur bureau graphique. La sélection concerne les applications compatibles et n'est pas une preuve de support du curseur dans la console.
