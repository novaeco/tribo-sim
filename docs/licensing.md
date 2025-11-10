# Politique de licence Terrarium S3

> **Licence principale :** MIT — voir [`LICENSE`](../LICENSE).

## Justification du choix MIT

- **Compatibilité commerciale** : autorise la distribution de firmwares préflashés, d’images OTA et de documentations dans des kits matériels.
- **Interopérabilité** : permet l’intégration avec des modules propriétaires (capteurs, passerelles cloud) sans obligation de publication du code dérivé.
- **Maintenance communautaire** : favorise les correctifs rapides par des intégrateurs tiers (installateurs, distributeurs) tout en conservant la traçabilité via les obligations de notice.

## Obligations à respecter

1. **Notice et disclaimer**
   - Inclure `© 2024-2025 NovaEco` et le texte complet de la licence MIT dans :
     - les dépôts Git dérivés,
     - les binaires distribués (fichiers `LICENSE` dans les archives OTA/ZIP),
     - les notices papier jointes au matériel (manuel utilisateur, fiche conformité).
   - Ne pas supprimer/altérer la clause de non-garantie.
2. **Documentation**
   - Reproduire l’encart de licence ajouté en tête de chaque document technique.
   - Pour les traductions, conserver la formulation légale anglaise telle quelle (ne pas la traduire mot pour mot).
3. **Firmware préinstallé**
   - Fournir un lien (QR code ou URL) vers la version exacte du code source correspondant aux binaires flashés.
   - En cas de modifications locales, publier les patches ou le fork public pendant la durée de distribution.
4. **Composants tiers**
   - `firmware/common/monocypher.*` est sous CC0 1.0. Conserver `monocypher_LICENSE` et la notice recommandée par l’auteur.
   - Ajouter un dossier `third_party/<lib>/LICENSE` pour tout nouveau composant embarqué.

## Processus de notification des contributeurs

1. **Identifier les auteurs** via `git shortlog -sne`.
2. **Créer une issue** par contributeur externe avec le modèle ci-dessous :
   ```markdown
   ## Consentement à la republication MIT
   Bonjour <@auteur>,
   Nous alignons tout le dépôt Terrarium S3 sur la licence MIT. Merci de confirmer que vos contributions
   (commits listés ci-dessous) peuvent être redistribuées sous MIT :
   - <hash1>: <résumé>
   - <hash2>: <résumé>

   Répondez par « Je consens » ou proposez une alternative.
   ```
3. **Archiver les réponses** dans `docs/legal/` (créer le dossier si besoin) ou dans l’issue GitHub correspondante.
4. **Absence de réponse** : après 30 jours, retirer ou réécrire le code concerné avant de publier sous MIT.

## Contributions futures

- Activer le DCO (`git config --global alias.ci "commit -s"`).
- Ajouter `Signed-off-by` sur chaque commit.
- Joindre `docs/licensing.md` lors des revues pour rappeler les obligations.

## FAQ interne

- **Puis-je mélanger MIT avec du GPLv3 ?** Oui, mais tout module GPLv3 contaminera la distribution correspondante. Préférer LGPL/BSD si une redistribution propriétaire est prévue.
- **Les schémas PCB sont-ils couverts ?** Oui, tant qu’ils sont versionnés dans ce dépôt. La licence MIT s’applique également aux fichiers Gerber/STEP fournis ici.
- **Que faire pour un partenariat OEM ?** Fournir un package `OEM_LICENSE.zip` contenant : `LICENSE`, `THIRD_PARTY.md`, firmwares signés, et un extrait de ce document rappelant les obligations.
