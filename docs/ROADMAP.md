# MiaouOS — Feuille de route

**Slogan :** « MiaouOS — Le Linux des chats ! »

MiaouOS vise une distribution indépendante construite à partir des sources amont, avec un noyau Linux, un espace utilisateur minimal, puis son propre environnement de bureau et son écosystème. Les fonctionnalités non implémentées restent explicitement marquées comme prévues.

## 0.1 — MiaouCore (priorité immédiate)
- [x] Pipeline de compilation : noyau Linux amont + BusyBox compilé depuis les sources.
- [x] Initramfs et interface console MiaouDesk.
- [x] Workflow CI avec test de démarrage QEMU et capture d'écran.
- [ ] Premier build CI entièrement vert.
- [ ] Vérifier le démarrage de MiaouDesk dans QEMU et contrôler le contenu de la capture.
- [ ] Ajouter des tests reproductibles du script init et de l'initramfs.

## 0.2 — Fondations du système
- [ ] Définir les conventions de configuration, journaux, services et chemins système.
- [ ] Ajouter un système de stockage persistant et un installateur sûr.
- [ ] Documenter les mises à jour, la récupération et les sommes de contrôle.
- [ ] Définir une politique de sécurité et de provenance des paquets.

## 0.3 — MiaouUI TUI
- [ ] Concevoir une bibliothèque TUI réutilisable : composants, layout, événements, focus et thèmes.
- [ ] Prendre en charge le clavier en premier, puis les séquences souris compatibles avec les terminaux.
- [ ] Créer des widgets : boutons, listes, champs, onglets, dialogues et barres de progression.
- [ ] Fournir des tests de rendu sur terminaux de tailles différentes.

## 0.4 — Paquets
- [ ] Définir un format .miaou versionné, des métadonnées et des sommes de contrôle.
- [ ] Concevoir un dépôt signé et un mécanisme de résolution des dépendances.
- [ ] Implémenter un moteur commun, appelé par miaou et miaou-get.
- [ ] miaou : interface conviviale et interactive.
- [ ] miaou-get : interface stable et scriptable.
- [ ] Ajouter ensuite Miaou Store, l'interface TUI de gestion des paquets.

## 0.5 — MiaouDesk graphique
- [ ] Choisir une pile graphique adaptée au matériel visé.
- [ ] Développer session, panneau/dock, lanceur et paramètres.
- [ ] Ajouter prise en charge de la souris, du tactile et de l'accessibilité.
- [ ] Construire un catalogue de thèmes de curseurs avec aperçu et retour arrière.

## 0.6 — Miaou Neko
- [ ] Créer un compagnon de bureau original inspiré des animaux de bureau classiques.
- [ ] Ajouter un interrupteur dans les paramètres, animations et préférences de comportement.
- [ ] Respecter les réglages d'accessibilité et permettre la désactivation complète.

## 0.7 — Compatibilité applicative
- [ ] Étudier Wine et les couches de compatibilité existantes avant de réinventer leur travail.
- [ ] Isoler les applications et documenter les limites, performances et dépendances.
- [ ] Évaluer séparément les composants de ReactOS : ce n'est pas une couche d'exécution Windows prête à intégrer directement.
- [ ] Ne jamais promettre une compatibilité universelle.

## Principes de qualité
- Construire et tester une fonctionnalité avant de la déclarer terminée.
- Ne pas confondre une maquette, un prototype console et un bureau graphique fonctionnel.
- Préserver la reproductibilité, la sécurité des mises à jour et la transparence sur les composants tiers.
