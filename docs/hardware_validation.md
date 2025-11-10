# Recommandations CEM et endurance

## Blindage et routage
- Utiliser un plan de masse continu sur les deux couches principales.
- Séparer les pistes rapides (SPI LCD, USB) des capteurs analogiques.
- Positionner des condensateurs de découplage 100 nF à moins de 2 mm de chaque VDD.
- Ajouter un blindage métallique autour du convertisseur DC/DC du dôme et relier au plan GND.
- Prévoir des ferrites 120 Ω @ 100 MHz sur les lignes d’alimentation des LED UV.

## Protection ESD / TVS
- Mettre des diodes TVS bidirectionnelles sur les connecteurs externes (USB, I²C sortie).
- Ajouter des résistances série 33 Ω sur SDA/SCL pour amortir les fronts.
- Utiliser des MOSFET à canal P avec Rds(on) < 30 mΩ pour le découplage batterie.

## Endurance thermique
- Vérifier la dissipation du radiateur par un cycle 60 °C / 20 %RH (8 h) suivi de 20 °C / 80 %RH (16 h) pendant 10 cycles.
- Appliquer une pâte thermique céramique (k > 3 W/mK) entre UVB LED et dissipateur.

# Procédure de burn-in
1. **Inspection visuelle** : vérifier soudure, polarité, connectique.
2. **Cycle électrique** : alimenter le contrôleur et le dôme 48 h à 90 % des courants nominaux.
3. **Stimulation climatique** : exécuter un script de variation CCT/UVA/UVB (voir `tools/burn_in.py`).
4. **Essai de vibrations** : 5 min/axe @ 5 gpp pour déceler faux contacts.
5. **Rapport** : consigner températures max, alarmes, redémarrages.

## Indicateurs à surveiller
- **Compteurs d’erreurs I²C / PWM** : exposés dans le regfile (`DOME_REG_DIAG_I2C_ERR_L/H`, `DOME_REG_DIAG_PWM_ERR_L/H`) et remontés par l’API `/api/status` (`dome.diagnostics.i2c_errors`, `pwm_errors`). Une hausse soudaine signale un câblage dégradé ou un driver LEDC/I²C instable.
- **Compteur d’interverrouillage** : `DOME_REG_DIAG_INT_COUNT_L/H` et `dome.diagnostics.interlock_count`. Doit rester à zéro hors essais de sécurité.
- **Historique des coupures UV** : buffer circulaire de 4 entrées (`dome.diagnostics.uv_cut_events`) avec timestamp (s depuis boot) et canaux impactés (`uva`, `uvb`). Permet de corréler un déclenchement UV avec les logs thermiques.
- **Commande de remise à zéro** : `POST /api/maintenance/reset_diagnostics` purge les compteurs et l’historique après une intervention. Le firmware remet à zéro `DOME_REG_DIAG_CMD` et réinitialise les registres associés.

# Tests CEM reproductibles
- Chambre anéchoïque 3 m, balayage 150 kHz – 1 GHz, marge > 6 dB par rapport à CISPR 32 classe B.
- Injection BCI 1 MHz – 400 MHz à 30 mA sur harness d’alimentation.
- ESD contact 8 kV sur châssis, air 15 kV sur surface tactile.

# Mode opératoire CEM (résumé)
1. Préparer configuration nominale (Wi-Fi actif, écran rafraîchi, UVB ON).
2. Connecter sonde de courant sur masse châssis -> châssis.
3. Documenter captures Spectrum + logs UART.
