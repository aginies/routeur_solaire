---
title: API & Diagnostics — Routeur Solaire
description: "Référence API REST du routeur PV : endpoint /status, calibration phase-angle, test Shelly, import/export de statistiques et diagnostic."
---

# API & Diagnostics

Le routeur expose une API REST et plusieurs endpoints de diagnostic pour le débogage en production. Cette page documente les interfaces accessibles via HTTP (généralement `http://<ip_du_routeur>/`).

## Endpoint `/status` — État Complet du Système

L'endpoint `GET /status` retourne un objet JSON avec l'état complet du routeur à un instant T :

### Données de Base

| Champ | Type | Description |
|-------|------|-------------|
| `grid_power` | float | Puissance réseau (W). Positif = import, Négatif = export. |
| `equipment_power` | float | Puissance totale redirigée (W) |
| `eq1_real_power` | float | Puissance réelle Eq1 mesurée (W) |
| `equip2_power` | float | Puissance Eq2 mesurée (W) |
| `boost_active` | bool | Boost manuel actif |
| `boost_remaining` | int | Minutes restantes du boost |
| `force_mode` | bool | Mode forcé actif |
| `emergency_mode` | bool | État d'urgence actif |
| `emergency_reason` | string | Raison de l'urgence (ex: "ESP32 Overheat!", "SSR Temp Sensor Fault!") |
| `ssr_temp` | float/null | Température SSR (°C), null si sonde déconnectée |
| `esp_temp` | float | Température interne ESP32 (°C) |
| `fan_active` | bool | État du ventilateur |
| `fan_percent` | int | Vitesse du ventilateur (0-100) |
| `grid_source` | string | Source de mesure : "JSY1", "JSY2", "MQTT", "HTTP" |
| `shelly_link` | bool | Shelly connecté |
| `shelly_error` | bool | Shelly en erreur |
| `mqtt_status` | bool | Broker MQTT connecté |
| `control_mode` | string | Mode SSR actuel : "trame", "burst", "phase", "cycle_stealing" |
| `night_mode` | bool | Mode nuit actif |
| `equip2_bypassed` | bool | Eq2 bypassé par météo |
| `equip2_max_power` | float | Puissance Eq2 configurée (W) |

### Données de Santé

| Champ | Type | Description |
|-------|------|-------------|
| `free_ram` | uint32 | RAM interne libre (octets) |
| `total_ram` | uint32 | RAM interne totale (octets) |
| `free_psram` | uint32 | PSRAM libre (octets) |
| `total_psram` | uint32 | PSRAM totale (octets) |
| `uptime` | uint32 | Temps de fonctionnement (secondes) |
| `rssi` | int | Puissance du signal WiFi (dBm) |
| `version` | string | Version du firmware |
| `build_time` | string | Date/heure de compilation |

### Indicateurs de Fonctionnalité

| Champ | Type | Description |
|-------|------|-------------|
| `stats_enabled` | bool | Statistiques activées (désactivé sur WROOM) |
| `history_enabled` | bool | Historique graphique activé (désactivé sur WROOM) |
| `data_log_enabled` | bool | Journal de données activé (désactivé sur WROOM) |
| `max_stats_days` | int | Jours de rétention de statistiques |
| `e_ssr_temp` | bool | Surveillance température SSR |
| `e_equip1` | bool | Équipement 1 activé |
| `e_equip2` | bool | Équipement 2 activé |
| `equip1_name` | string | Nom de l'équipement 1 |
| `equip2_name` | string | Nom de l'équipement 2 |
| `shelly_mqtt_enabled` | bool | Mode MQTT Shelly activé |
| `mqtt_enabled` | bool | MQTT activé |

### Données AC (Phase)

Toutes les données AC sont regroupées sous l'objet `ac` :

| Champ | Type | Description |
|-------|------|-------------|
| `ac.zx_counter` | uint32 | Nombre total de passages par zéro (cumulatif) |
| `ac.zx_last_us` | uint32 | Dernier passage par zéro (microsecondes) |
| `ac.half_cycle_parity` | string | Parité du dernier demi-cycle ("odd" ou "even") |
| `ac.full_cycle_index` | int | Index du cycle complet actuel |
| `ac.grid_hz_est` | float | Fréquence estimée du réseau (Hz) |

### Mode Trame (Bresenham)

Données regroupées sous l'objet `trame` :

| Champ | Type | Description |
|-------|------|-------------|
| `trame.enabled` | bool | Mode Trame actif |
| `trame.fire_full_cycle` | bool | Feux de cycle complet en cours |
| `trame.decision_on_odd_cross` | bool | Décision sur passage impair |
| `trame.decision_age_ms` | uint32 | Âge de la dernière décision (ms) |

### Contrôle de Phase

Données regroupées sous l'objet `phase` :

| Champ | Type | Description |
|-------|------|-------------|
| `phase.enabled` | bool | Mode Phase actif |
| `phase.timer_arm_pending` | bool | Timer en attente d'armage |
| `phase.last_requested_wait_us` | int | Dernier délai demandé (µs) |

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
