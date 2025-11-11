# Sécurité TLS & Gestion des secrets

> **Licence :** MIT — voir [`LICENSE`](../LICENSE). Inclure cette notice lors de la diffusion de ces procédures internes/externe.

## Provisionnement automatique

- **TLS** : au premier démarrage, le contrôleur génère une paire RSA‑2048 et un certificat X.509 auto-signé (CN `terrarium-controller`).
  - Validité : 180 jours (renouvellement automatique si la date courante dépasse `J-30`).
  - Stockage : partition NVS chiffrée (`nvs_keys` + `nvs` encrypté via `CONFIG_NVS_ENCRYPTION`).
- **API Bearer token** : un secret hexadécimal de 64 caractères est généré, salé (SHA-256) et stocké en NVS chiffré.
  - Le token en clair **n'est plus journalisé** : au boot, un log prévient simplement qu'il est disponible via la console série sécurisée (`secure> token`).
  - Toute requête HTTPS doit inclure `Authorization: Bearer <token>`.

## Console série sécurisée

- La console USB CDC embarque un REPL restreint (`prompt secure>`), uniquement accessible depuis le port maintenance physique.
- Commande disponible :

  ```text
  secure> token           # Affiche le jeton s'il n'a jamais été divulgué
  secure> token --rotate  # Invalide l'ancien secret et affiche le nouveau jeton
  ```

- Chaque jeton n'est affiché qu'une seule fois. Copiez-le immédiatement dans un coffre (Vault, Bitwarden…).
- La rotation via `--rotate` force la génération d'un nouveau secret (mêmes garanties cryptographiques que lors du provisionnement initial).

## Rotation manuelle

Endpoint REST authentifié (`POST /api/security/rotate`) :

```bash
curl -k https://terrarium-s3.local/api/security/rotate \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"rotate_cert": true, "rotate_token": true}'
```

Réponse :

```json
{"rotate_cert":true,"rotate_token":true,"token":"<nouveau_token>"}
```

- `rotate_cert` : régénère le certificat (déployez le nouveau fingerprint côté client).
- `rotate_token` : invalide l’ancien secret et retourne le nouveau token (affiché **une seule fois**).
- Le endpoint accepte des payloads partiels (`{"rotate_token": false}` pour ne remplacer que le certificat).

## Bonnes pratiques

1. **Inventoriez** les empreintes SHA-256 des certificats côté CI/CD (voir workflow `controller-security`).
2. **Renouvelez** le token dès qu’un opérateur quitte l’équipe ou qu’un client API est compromis.
3. **N’encodez jamais** le token dans un firmware ou une application distribuée.
4. **Limitez** les droits réseau : firewall → n’autoriser que les IPs de supervision.
5. **Surveillez** les logs (`idf.py monitor`) : 3 erreurs `401 Unauthorized` consécutives doivent déclencher une alerte SOC.

## Procédure de récupération

1. Connectez-vous en USB (`idf.py -p /dev/ttyACM0 monitor`) et saisissez `token` pour afficher le secret actif.
2. Si le token est perdu :
   - Utiliser `token --rotate` depuis la console sécurisée pour invalider l'ancien secret.
   - À défaut d'accès physique, un client disposant encore du token courant peut appeler `POST /api/security/rotate`.
   - En ultime recours : `idf.py erase_flash` efface tous les secrets (reprovisionnement complet requis).

## CI/CD

- Workflow `controller-security` (GitHub Actions) :
  - Build contrôleur via ESP-IDF (`idf.py build`).
  - Exécution du test Unity `test_http_security` garantissant le refus sans bearer token.
- Ajoutez votre propre pipeline pour distribuer le nouveau token aux services backend (HashiCorp Vault, AWS Secrets Manager, etc.).

> **Important** : les certificats et secrets sont stockés dans `nvs` **chiffré** (AES-XTS 256). Conservez la partition `nvs_keys` en lieu sûr lors des procédures de reflash usine.
