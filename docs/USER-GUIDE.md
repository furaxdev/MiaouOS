# Guide utilisateur MiaouOS (prototype)

## Démarrage
MiaouOS démarre directement sur **MiaouDesk**, le bureau graphique (si le noyau fournit un framebuffer ; sinon, repli sur la console).

- **Super** (touche Windows) : menu des applications, avec recherche. Clic droit sur le bureau : menu contextuel.
- **Alt+Tab** : changer de fenêtre · **Alt+F4** : fermer · **Ctrl+Alt+T** : terminal · **Super+←/→/↑/↓** : aimanter, agrandir, réduire.
- Le clavier est en AZERTY par défaut ; basculez QWERTY/AZERTY depuis le panneau (clic sur « FR ») ou les Paramètres.
- Pour revenir à la console : menu → *Console*.

Voir `docs/GRAPHICAL-DESKTOP.md` pour la liste complète et les limites.

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

Dans MiaouDesk, Neko est un petit chat qui suit le curseur (réglage dans Paramètres → Miaou Neko). `miaou-neko play` reste l'animation console.

## Curseurs
- `miaou-cursors list`
- `miaou-cursors preview THEME`
- `miaou-cursors set THEME`

La prévisualisation graphique requiert le futur bureau graphique. La sélection concerne les applications compatibles et n'est pas une preuve de support du curseur dans la console.
