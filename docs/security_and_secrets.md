# Sécurité TLS & Gestion des secrets

> **Licence :** MIT — voir [`LICENSE`](../LICENSE). Inclure cette notice lors de la diffusion de ces procédures internes/externe.

## Provisionnement automatique

- **TLS** : au premier démarrage, le contrôleur génère une paire RSA‑2048 et un certificat X.509 auto-signé (CN `terrarium-controller`).
  - Validité : 180 jours (renouvellement automatique si la date courante dépasse `J-30`).
  - Stockage : partition NVS chiffrée (`nvs_keys` + `nvs` encrypté via `CONFIG_NVS_ENCRYPTION`).
- **API Bearer token** : un secret hexadécimal de 64 caractères est généré, salé (SHA-256) et stocké en NVS chiffré.
  - Le token en clair est **imprimé une seule fois** sur la console USB (`HTTP API bootstrap token`). Sauvegardez-le dans un coffre (Vault, Bitwarden…).
  - Toute requête HTTPS doit inclure `Authorization: Bearer <token>`.

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

1. Connectez-vous en USB et capturez le nouveau token (`idf.py -p /dev/ttyACM0 monitor`).
2. Si le token est perdu :
   - Redémarrer en mode maintenance.
   - Exécuter `curl -k ... rotate_token true` via un client possédant encore l’ancien token.
   - En ultime recours : `idf.py erase_flash` efface toutes les secrets (reprovisionnement complet requis).

## CI/CD

- Workflow `controller-security` (GitHub Actions) :
  - Build contrôleur via ESP-IDF (`idf.py build`).
  - Exécution du test Unity `test_http_security` garantissant le refus sans bearer token.
- Ajoutez votre propre pipeline pour distribuer le nouveau token aux services backend (HashiCorp Vault, AWS Secrets Manager, etc.).

> **Important** : les certificats et secrets sont stockés dans `nvs` **chiffré** (AES-XTS 256). Conservez la partition `nvs_keys` en lieu sûr lors des procédures de reflash usine.
