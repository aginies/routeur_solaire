# API & Diagnostics

Le routeur expose une API REST et plusieurs endpoints de diagnostic pour le débogage en production. Cette page documente les interfaces accessibles via HTTP (généralement `http://<ip_du_routeur>/`).

## Endpoint `/status` — État Complèt du Système

L'endpoint `GET /status` retourne un objet JSON avec l'état complet du routeur à un instant T :

### Données de Base

| Champ | Type | Description |
|-------|------|-------------|
| `state_name` | string | Nom de l'état actuel (NORMAL, BOOST, NIGHT, EMERGENCY_FAULT, SAFE_TIMEOUT) |
| `ssr_duty_pct` | float | Pourcentage de puissance SSR (0–100%) |
| `eq2_status` | string | État de l'équipement 2 |

### Métriques Réseau & Production

| Champ | Type | Description |
|-------|------|-------------|
| `grid_power` | float | Puissance réseau (W). Positif = export, Négatif = import. |
| `eq1_power` | float | Puissance de l'équipement 1 (W) |
| `eq2_power` | float | Puissance de l'équipement 2 (W) |

### Températures

| Champ | Type | Description |
|-------|------|-------------|
| `ssr_temp_celsius` | float | Température SSR (°C) — mesure externe via DS18B20 |
| `env_temp_celsius` | float | Température ambiante (°C) — autre sonde DS18B20 |
| `esp32_temp` | float | Température interne de l'ESP32 (°C) |

### Données AC (Phase)

| Champ | Type | Description |
|-------|------|-------------|
| `half_cycle_parity` | string | Parité du dernier demi-cycle ("odd" ou "even") |
| `zx_counter` | uint64 | Nombre total de passages par zéro détectés (cumulatif) |
| `grid_hz_est` | float | Fréquence estimée du réseau (Hz), dérivie des interruptions ZX |

### Métriques Météo

| Champ | Type | Description |
|-------|------|-------------|
| `weather_clouds_low` | int | Couverture nuageuse basse (%) |
| `weather_clouds_mid` | int | Couverture nuageuse moyenne (%) |
| `weather_clouds_high` | int | Couverture nuageuse haute (%) |
| `weather_solar_confidence` | float | Indice de confiance solaire (0–100) |
| `weather_age` | uint32 | Âge des données météo en secondes |
| `weather_icon` | string | Icône météo actuelle (emoji ou nom du code) |

### Métriques RTC & Version

| Champ | Type | Description |
|-------|------|-------------|
| `rtc_time` | string | Heure RTC (`%H:%M`) — horloge interne ESP32 |
| `rtc_date` | string | Date RTC (`%d/%m/%Y`) |
| `firmware_version` | string | Version compilée du firmware |

### Indicateurs de Fonctionnalité

| Champ | Type | Description |
|-------|------|-------------|
| `stats_enabled` | bool | Les statistiques sont activées (désactivé sur WROOM) |
| `history_enabled` | bool | L'historique graphique est activé (désactivé sur WROOM) |
| `data_log_enabled` | bool | Le journal de données est activé (désactivé sur WROOM) |

### Validation des Broches (`pin_validation`)

Chaque broche configurée est vérifiée pour détecter les conflits :

```json
"pin_validation": {
  "SSR": {"valid": true},
  "RELAY": {"valid": true},
  "FAN_PWM": {"valid": true},
  "ZX_INPUT": {"valid": false, "reason": "Pin conflict with JSY1_RX"},
  ...
}
```

---

## Page Développeur (`/web_dev`)

La page cachée `/web_dev` offre un tableau de bord interactif en temps réel :
- État des capteurs et actionneurs (SSR, relais, ventilateur)
- Puissance réseau mesurée vs estimée
- Journal d'événements (logs) accessible via téléchargement

---

## Calibration Phase-Angle (`/web_phase_cal`)

Interface de calibration pour le mode Contrôle de Phase :
- Lance un balayage automatique sur une plage de delays (µs).
- Affiche en temps réel les résultats : puissance réseau, puissance équipement 1, delay actuel.
- Boutons pause/resume/jump pendant l'exécution.
- Les paramètres sont sauvegardés dans `config.json` et le routeur reboot après calibration.

---

## Rafraîchissement Météo (`/weather_refresh`)

Endpoint `GET /weather_refresh` déclenche un appel immédiat à l'API Open-Meteo, en ignorant le cycle programmé (environ toutes les 15 minutes). Utile pour tester une nouvelle configuration météo ou obtenir des données fraîches après un changement de localisation.

---

## Calibration d'Angle de Phase (`/api/phase_cal`)

Endpoint POST avec actions JSON :
- `start` — Lance le balayage (avec paramètres optionnels : min, max, step, hold).
- `pause` / `resume` / `jump N` — Contrôle en cours d'exécution.
- `exit` — Arrête et reboot.
- `status` — Retourne l'état actuel avec gridPower, equipPower, progress.

---

## Test de Santé Shelly (`/test_shelly`)

Endpoint POST : `/test_shelly?target=em|eq1|eq2`  
Effectue un contrôle à la demande du dispositif Shelly configuré. Retourne le JSON avec puissance, tension et génération (Gen1 ou Gen2). Utile pour diagnostiquer les problèmes de connectivité sans attendre le prochain cycle de polling automatique.

---

## Endpoints de Migration & Restauration

### Import de Statistiques (`/import_stats`)
Endpoint POST qui accepte un fichier `stats.json` au format attendu par le routeur (max 200 Ko). L'upload est atomique : écrit dans un fichier temporaire puis renomme. Redémarre le routeur en cas de succès.

### Téléchargement des Données (`/download_data`)
Retourne les fichiers `log.txt` et `solar_data.json` pour analyse hors-ligne ou sauvegarde locale.

---

## Limitations Connues

- **Topic MQTT** : Les topics dépassant 128 caractères sont tronqués silencieusement, ce qui peut causer des échecs de publication LWT avec un `mqtt_name` long.
- **Rate limiting** : Les opérations save/reboot sont limitées à une fois toutes les 60 secondes (HTTP 429 sinon).
- **WROOM** : La version ESP32-WROOM compile avec les flags `-D DISABLE_STATS`, `-D DISABLE_HISTORY`, `-D DISABLE_DATA_LOG`. L'endpoint `/status` retourne `stats_enabled: false`, etc.
