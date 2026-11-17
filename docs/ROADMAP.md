# MiaouOS — feuille de route

MiaouOS doit devenir un système Linux réellement utilisable, accessible et distinctif. Les cases cochées désignent des prototypes présents dans le dépôt, pas une validation de production.

## Noyau et démarrage
- [x] Compilation du noyau et BusyBox en CI.
- [x] Initramfs et lancement de MiaouDesk en console.
- [x] Tests QEMU avec captures PNG réelles et interactions souris/clavier (CI).
- [ ] Tests QEMU entièrement verts et reproductibles pour chaque nouvelle version.
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
- [x] Gestionnaire de fenêtres framebuffer : déplacement, redimensionnement 8 bords, aimantation, minimisation, maximisation, fermeture, Alt+Tab.
- [x] Panneau de type Plasma, lanceur avec recherche, icônes de bureau, menu contextuel, notifications, thèmes clair/sombre, accent, échelle automatique.
- [x] Entrées : clavier AZERTY/QWERTY (codes bruts), souris/tablette/tactile via evdev.
- [x] Terminal (pty), Fichiers, Notes, Calculatrice, Visionneuse, Horloge, Navigateur texte, Boutique (miaou-get), Moniteur, Réseau (DHCP), Paramètres.
- [x] Écran de connexion, comptes (SHA-256 salé), verrouillage, bureaux virtuels, centre de contrôle, « Exécuter », animations.
- [ ] Stockage persistant (comptes, fichiers, réglages) et chiffrement.
- [ ] Serveur graphique/compositeur et sessions.
- [ ] Navigateur avec moteur web (CSS, JavaScript, images), HTTPS vérifié.
- [ ] Presse-papiers, glisser-déposer, accessibilité, son.
- [ ] Bluetooth, Wi-Fi, multi-écran via DRM/KMS, changement de résolution à chaud.

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
