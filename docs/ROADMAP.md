# MiaouOS — feuille de route

MiaouOS doit devenir un système Linux réellement utilisable, accessible et distinctif. Les cases cochées désignent des prototypes présents dans le dépôt, pas une validation de production.

## Noyau et démarrage
- [x] Compilation du noyau et BusyBox en CI.
- [x] Initramfs et lancement de MiaouDesk en console.
- [ ] Tests QEMU entièrement verts et reproductibles.
- [ ] Comptes utilisateurs, permissions, journaux, réseau et arrêt propre.
- [ ] Détection matérielle et installation sur de vraies machines.

## Miaou Package Manager
- [x] Format local .miaou, moteur partagé, miaou et miaou-get.
- [x] Recherche locale, installation et suppression isolées.
- [ ] Signatures, index signé et dépôt réseau.
- [ ] Résolution de dépendances, conflits, mises à jour atomiques et retour arrière.

## MiaouUI et Store
- [x] Primitives console et menu clavier.
- [ ] Navigation souris, focus, listes, champs, dialogues, onglets.
- [ ] Toolkit graphique partagé et catalogue en ligne.

## MiaouDesk graphique
- [ ] Serveur graphique/compositeur, sessions et fenêtres.
- [ ] Panneau, dock, lanceur, notifications et paramètres.
- [ ] Gestionnaire de fichiers, terminal, éditeur, navigateur et accessibilité tactile.

## Miaou Cursors
- [x] Détection et sélection de thèmes de curseurs existants.
- [ ] Catalogue officiel et aperçu graphique.

## Miaou Neko
- [x] Réglage on/off.
- [ ] Compagnon animé réel intégré au bureau.

## Compatibilité Windows
- [x] Stratégie documentée.
- [ ] Intégration optionnelle de Wine, détection et tests de compatibilité.

## Distribution
- [ ] Tests complets, ISO BIOS/UEFI, sommes de contrôle et récupération.
- [ ] Installateur, documentation complète et versions stables.
