---
title: Journal des Modifications — Routeur Solaire
description: Historique des versions et évolutions du firmware Routeur Solaire.
---

# Journal des Modifications

Ce document retrace les évolutions majeures du firmware. Pour l'historique complet, consultez les [releases GitHub](https://github.com/aginies/routeur_solaire/releases).

## Récentes

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
