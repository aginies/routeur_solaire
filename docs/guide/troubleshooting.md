---
title: Dépannage — Routeur Solaire
description: "Résolution des problèmes courants du routeur PV : démarrage, Wi-Fi, capteurs, SSR, mémoire et mises à jour."
---

# Dépannage

Cette page regroupe les problèmes courants et leurs solutions.

## L'ESP32 ne démarre pas

| Symptôme | Cause probable | Action |
|----------|----------------|--------|
| LED fixe éteinte | Alimentation insuffisante | Vérifiez l'alimentation 3.3V/5V et les soudures |
| LED clignote rapidement | État EMERGENCY_FAULT | Consultez le `/status` JSON pour identifier la cause |
| Pas de sortie série | Mode USB-CDC non configuré | Vérifiez `ARDUINO_USB_CDC_ON_BOOT` dans `platformio.ini` |

## Connexion Wi-Fi et accès Web

| Symptôme | Cause probable | Action |
|----------|----------------|--------|
| Pas d'accès Web | ESP32 en mode AP | Connectez-vous au réseau `RouteurSolaire_XXXX` |
| IP changeante | DHCP non configuré | Configurez une IP fixe dans les paramètres Wi-Fi |
| Déconnexions fréquentes | Signal faible | Positionnez l'antenne ou rapprochez l'ESP32 |

## Capteur d'énergie

| Symptôme | Cause probable | Action |
|----------|----------------|--------|
| `Shelly Timeout!` répété | Shelly hors ligne ou réseau saturé | Augmentez `safety_timeout`, vérifiez le ping |
| Puissance réseau incorrecte | Mauvaise configuration du compteur | Vérifiez le type de sensor et les broches UART |
| JSY non détecté | Câblage UART inversé | Vérifiez TX → RX et RX → TX |
| Dual JSY incohérent | Canal mal assigné | Vérifiez `jsy_grid_channel` et `jsy_equip1_channel` |

## SSR et Température

| Symptôme | Cause probable | Action |
|----------|----------------|--------|
| `SSR Temp Sensor Fault!` | DS18B20 déconnectée | Vérifiez la résistance pull-up 4,7 kΩ et la broche IO |
| SSR ne chauffe pas | Mode NIGHT actif ou consigne à 0 | Vérifiez l'heure et le surplus solaire détecté |
| Surchauffe fréquente | Ventilation insuffisante | Vérifiez le ventilateur et le heatsink |
| Puissance SSR incohérente | Mode SSR mal configuré | Essayez le mode Burst ou Cycle Stealing |

## Mémoire et Stockage

| Symptôme | Cause probable | Action |
|----------|----------------|--------|
| Erreurs LittleFS | Flash corrompu | Exécutez `./flash.sh --erase` |
| PSRAM non détectée | Mauvaise variante | Vérifiez `-v N16R8` ou `-v N8R2` |
| Stats perdues au reboot | NVS corrompue | Exécutez `./flash.sh --erase` |
| VERSION mismatch | Flash sans LittleFS | Flashez toujours firmware + LittleFS |

## Mode Vacances et Forçage Horaire

| Symptôme | Cause probable | Action |
|----------|----------------|--------|
| Forçage horaire ignoré | Mode Vacances actif | Le mode Vacances désactive tous les forçages |
| Pas de chauffe en heures creuses | Vérifiez le créneau de forçage | Configurez un horaire valide dans l'interface |

## Mises à jour

| Symptôme | Cause probable | Action |
|----------|----------------|--------|
| OTA échoue | Fichier .bin corrompu | Retéléchargez le firmware depuis PlatformIO |
| Pages web ne changent pas | LittleFS non mis à jour | Exécutez `pio run -t uploadfs` |

## Plus d'aide

- Consultez les logs via `/web_dev` ou téléchargez `log.txt`
- Utilisez `GET /status` pour inspecter l'état complet du système
- Consultez la page [API & Diagnostics](./diagnostics.md) pour les endpoints de débogage
