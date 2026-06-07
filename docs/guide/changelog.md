---
title: Journal des Modifications — Routeur Solaire
description: Historique des versions et évolutions du firmware Routeur Solaire.
---

# Journal des Modifications

Ce document retrace les évolutions majeures du firmware. Pour l'historique complet, consultez les [releases GitHub](https://github.com/aginies/routeur_solaire/releases).

## Récentes

### v1.1.0

**Features :**
- GitHub Actions workflow pour builds automatiques sur tag (`v*`) avec releases GitHub
- Support écran LCD 1602A I2C (défilement SSID+IP, puissance réseau/redirigée)
- Flag `--build` dans `flash.sh` pour compilation seule

**Fixes :**
- Bugs critiques : link error, GPIO6 bloqué, conflit pin ventilateur, timeout scan I2C, clamp equip2_priority, guard ssr_pin, validation addr I2C, champs LCD dans API
- Thread safety : abs→fabsf, flush avant reset_config, clamp azimuth, floats atomiques, section critique ISR
- LcdManager : fuite mémoire, taille d'affichage configurable, persistance config LCD
- Logger : écritures atomiques, mutex maintenu via flush, flux sous verrou
- WebManager : volatile _rebootRequested, remplacement atomique stats.json
- Shelly : ajout champ SHELLY_TIMEOUT, correction double-save save_config_eq2
- Stats : correction vue annuelle, fallback HTTPClient dans WeatherManager
- Native : `-e native` exécute les tests au lieu de quitter silencieusement
- docs : ajout seoPlugin.js manquant, correction commentaire streamLogs

### v0.9.x — Dernières améliorations

- Refonte de l'interface web et du thème VitePress
- Amélioration de la détection de passages par zéro
- Optimisations de performance sur l'ESP32-S3
- Corrections de stabilité dans le gestionnaire d'états
- Support écran LCD 1602A I2C (défilement SSID+IP, puissance réseau/redirigée)

### v0.8.x — Fonctionnalités principales

- Support du JSY-MK-194 en UART natif
- Calibration d'angle de phase automatique
- Mode Force (Boost Manuel + Forçage Horaire)
- Mode Vacances
- Intégration Open-Meteo (confiance solaire)
- Support dual JSY (deux compteurs simultanés)

### v0.7.x — Sécurité

- Machine à états avec priorités (NORMAL → BOOST → NIGHT → EMERGENCY_FAULT)
- Hystérésis de récupération thermique (5°C)
- Relais de sécurité fail-safe
- Rate limiting sur les opérations de sauvegarde

## Historique complet

Consultez les [tags GitHub](https://github.com/aginies/routeur_solaire/tags) pour l'historique complet des versions.
