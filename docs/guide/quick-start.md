---
title: Guide de Démarrage Rapide — Routeur Solaire
description: "Guide de démarrage rapide du routeur PV : compilation, flashage, configuration Wi-Fi et premier démarrage en 5 minutes."
---

# Guide de Démarrage Rapide

Suivez ces étapes pour mettre en route votre routeur PV en moins de 5 minutes.

## Prérequis

- Un module **ESP32-S3** (recommandé : ESP32-S3-WROOM-1-N16R8)
- Un câble USB-C
- Un compteur d'énergie (JSY-MK-194G recommandé, ou Shelly EM)
- Un SSR (Solid State Relay) et une charge résistive (chauffe-eau, etc.)

## Étape 1 — Compilation et flashage

Clonez le dépôt et flashez le firmware :

```bash
git clone https://github.com/aginies/routeur_solaire.git
cd routeur_solaire

# Compilez et flashez (ESP32-S3 avec variante N16R8)
./flash.sh -v N16R8 -m
```

> Consultez la page [Installation](./installation.md) pour les options avancées et le dépannage.

## Étape 2 — Connexion au point d'accès

Au premier démarrage, l'ESP32 crée un point d'accès Wi-Fi nommé **`RouteurSolaire_XXXX`** (où XXXX sont les 4 derniers chiffres de l'adresse MAC).

Connectez-vous à ce réseau et ouvrez `http://192.168.4.1` dans votre navigateur.

## Étape 3 — Configuration initiale

Sur la page de configuration :

1. **Wi-Fi** — Entrez le SSID et mot de passe de votre réseau domestique.
2. **Capteur réseau** — Sélectionnez votre type de compteur (JSY-MK-194 ou Shelly EM).
3. **Équipement 1** — Configurez la puissance de votre chauffe-eau et le mode SSR souhaité.
4. **Consigne réseau** — Mettez `0` pour le "Zéro Export" (tout excédent est redirigé).

Sauvegardez et le routeur redémarre sur votre réseau Wi-Fi.

## Étape 4 — Accès à l'interface

Retrouvez votre routeur sur le réseau local à l'adresse IP que vous avez notée. L'interface web vous permet de :

- Visualiser la production solaire et le routage en temps réel
- Consulter les statistiques d'économies
- Modifier la configuration à tout moment

## Prochaines étapes

- [Configuration détaillée](./configuration.md) — Modes SSR, planning horaire, anticipation météo
- [Sécurité & Maintenance](./safety.md) — Protection thermique, mode vacances
- [Intégration MQTT](./configuration.md#mqtt--domotique) — Pour Home Assistant et autres systèmes domotiques
