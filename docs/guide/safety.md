---
title: Sécurité & Maintenance — Routeur Solaire
description: "Sécurité du routeur PV : machine à états, protection thermique, hystérésis, mode vacances, authentification web et mises à jour OTA."
---

# Sécurité & Maintenance

Le routeur intègre plusieurs couches de protection pour garantir un fonctionnement fiable et sécurisé, tant en conditions normales qu'en cas de défaut. Cette page décrit le comportement de ces mécanismes.

## États du Système (State Machine)

Le système fonctionne selon une machine à états avec quatre priorités :

| État | Description | SSR Duty Cycle | Relais Sécurité |
|------|-------------|----------------|-----------------|
| **NORMAL** | Fonctionnement normal — le PID régule la puissance | Contrôlé par PID | Fermé (alimenté) |
| **BOOST** | Mode Boost Manuel ou Forçage Horaire activés | Limitée à `max_duty_percent` | Fermé (alimenté) |
| **NIGHT** | Mode Nuit actif — aucune production solaire | 0% | Ouvert (coupé) |
| **EMERGENCY_FAULT** | Défaut critique détecté | 0% | Ouvert (coupé) |
| **SAFE_TIMEOUT** | Perte de contact avec le capteur Shelly | 0% | Ouvert (coupé) |

### Ordre des Priorités

1. **Priorité 0 — EMERGENCY_FAULT** : Surchauffe (ESP32 ou SSR), défaut du capteur de température SSR, timeout Shelly.
2. **Priorité 1 — SAFE_TIMEOUT** : Le capteur Shelly n'a pas été pollé dans les `safety_timeout` secondes.
3. **Priorité 2 — BOOST** : Mode Boost Manuel (`boost_active`) ou Forçage Horaire (`forcedWindow`).
4. **Priorité 3 — NIGHT** : Mode Nuit (hors heures solaires).

---

## Types d'Urgence (Emergency Kinds)

Quatre causes peuvent déclencher un état d'urgence. Chaque cause génère une raison lisible dans les logs et le `/status` JSON.

| Cause | Raison (log) | Détail |
|-------|--------------|--------|
| `ESP_OVERHEAT` | « ESP32 Overheat! » | La température interne du microcontrôleur dépasse `max_esp32_temp` (défaut : 65°C). |
| `EXT_OVERHEAT` | « External Overheat! » | Le capteur DS18B20 sur la sonde SSR détecte une température supérieure à `ssr_max_temp`. |
| `SSR_FAULT` | « SSR Temp Sensor Fault! » | La sonde DS18B20 renvoie une température inférieure à -100°C, indiquant qu'elle est déconnectée. |
| `SHELLY_TIMEOUT` | « Shelly Timeout! » | Le capteur Shelly n'a pas répondu dans le délai `safety_timeout` secondes. |

---

## Hystérésis de Récupération (5°C)

Une fois une alarme thermique déclenchée, le système utilise une **hystérésis de 5°C** pour la récupération afin d'éviter les réarmements trop fréquents :

- **Allumage** : Se produit quand la température dépasse `max_esp32_temp` (défaut 65°C).
- **Extinction** : Ne se produit que quand la température redescend en dessous de `max_esp32_temp - 5.0°C` (soit 60°C dans le cas par défaut).

Ce comportement s'applique à la fois pour l'ESP32 interne (`max_esp32_temp`) et pour la sonde SSR externe (`ssr_max_temp`).

### Exemple pratique

Si vous configurez `max_esp32_temp = 70°C` :
- Le système entre en état d'urgence à **70°C**.
- Il ne revient à l'état NORMAL qu'à **65°C** (70 - 5).
- Entre les deux, le SSR reste coupé et le relais de sécurité ouvert.

> **Note :** Cette hystérésis est stockée en mémoire au moment de `SafetyManager::evaluateState()`. Si la température fluctue autour du seuil sans descendre sous le seuil d'hystérésis pendant 60 secondes (intervalle de polling), l'état d'urgence persistera.

---

## Séquence de Récupération en Cas d'Urgence

Quand un `STATE_EMERGENCY_FAULT` ou `STATE_SAFE_TIMEOUT` est détecté, le système exécute cette séquence :

1. **`ActuatorManager::setDuty(0.0)`** — Le cycle de commande du SSR passe à 0%.
2. **`digitalWrite(ssr_pin, LOW)`** — La broche de contrôle du SSR est forcée en bas.
3. **`ActuatorManager::openRelay()`** — Le relais de sécurité s'ouvre, coupant physiquement l'alimentation du SSR.

Cette séquence garantit que même si le firmware plante par la suite (rare), la charge est déjà déconnectée matériellement.

---

## Mode Vacances : Comportement Détaillé

Le mode vacances peut être activé via l'interface web ou directement par API REST :

### API REST

| Endpoint | Méthode | Description |
|----------|---------|-------------|
| `/set_vacation?days=X` | GET | Active le mode vacances pour X jours à partir de maintenant. Retourne « Vacances configurées ». |
| `/cancel_vacation` | GET | Annule immédiatement le mode vacances. Retourne « Vacances annulées ». |

### Interaction avec les Forçages Horaires

Le mode vacances **désactive systématiquement** tous les forçages horaires :

```cpp
bool effectiveForced = forcedWindow && !vacationMode;
```

Même si un créneau de forçage horaire est programmé (ex: heures creuses à 23h), il sera ignoré pendant le mode vacances. Le routeur ne fait rien — ni chauffage, ni routage vers l'équipement 2.

---

## Authentification Web

Le routeur peut exiger une authentification pour accéder aux pages de configuration et aux endpoints API :

- **`web_user`** / **`web_password`** : Configurez un nom d'utilisateur et mot de passe dans les paramètres web.
- Par défaut, ces champs sont vides → aucune authentification requise.
- Dès qu'un utilisateur est défini, le routeur exige une connexion HTTP (Basic Auth) sur :
  - Les pages de configuration (`/config`, `/equip2`)
  - Les endpoints de sauvegarde (`/save_config`, `/save_eq2_schedule`)
  - L'API de calibration phase-angle (`/api/phase_cal`)
  - La page des statistiques et la page développeur

> **Note :** Le login est demandé au premier chargement d'une page protégée. Votre navigateur le mémorise via `Authorization: Basic ...` pour les requêtes suivantes.

---

## Logs & Fichiers de Données

Le routeur conserve deux fichiers principaux sur son système de fichiers LittleFS :

### `log.txt` — Journal des événements

Fichier texte avec horodatage (`%Y-%m-%d %H:%M:%S`). Chaque ligne contient le niveau (INFO/ERROR/WARN) et un message. Téléchargeable via l'interface web.

### `solar_data.json` — Statistiques

Conteneur JSON structuré en tableaux `import`, `redirect`, `export`. Chaque entrée correspond à une journée avec horodatage Unix. Contient également les totaux quotidiens (`total_import_today`, etc.) mis à jour toutes les 60 secondes.

#### Import/Export de Statistiques

**Restauration d'un fichier** : Le endpoint `POST /import_stats` accepte un fichier JSON au format `stats.json` (max 200 Ko). Il écrit atomiquement dans un fichier temporaire puis renomme, et redémarre le routeur en cas de succès. Utilisé pour migrer ou restaurer des données historiques.

---

## Mises à jour OTA

Le firmware peut être mis à jour directement via le navigateur web sans connexion USB :

1. Téléchargez le fichier `.bin` compilé depuis PlatformIO (`pio run --environment s3 -t upload`).
2. Allez dans la section « Tools » de l'interface web.
3. Sélectionnez le fichier et cliquez sur « Update Firmware ».

> **Attention :** Après un flash OTA, vérifiez que le système de fichiers LittleFS est également mis à jour (upload via `pio run -t uploadfs`) pour éviter les problèmes d'incompatibilité entre firmware et pages web.

---

## Dépannage des Défauts

| Symptôme | Cause probable | Action |
|----------|----------------|--------|
| LED interne clignote rapidement | État EMERGENCY_FAULT détecté | Vérifiez la température SSR dans le `/status` JSON |
| `SSR Temp Sensor Fault!` dans les logs | DS18B20 déconnectée ou câblage défaillant | Vérifiez la résistance de pull-up 4,7 kΩ et la broche IO16/IO23 |
| `Shelly Timeout!` répété | Shelly hors ligne ou réseau saturé | Augmentez `safety_timeout`, vérifiez le ping vers le Shelly |
| Configuration non sauvegardée | Action répétée dans les 60 secondes précédentes | Attendez 1 minute avant de réessayer |
