# Introduction

## Vue d'ensemble

Le **Routeur Solaire** est une implémentation C++ haute performance d'un routeur photovoltaïque, optimisée pour la plateforme ESP32. Il surveille en temps réel l'énergie exportée vers le réseau par vos panneaux solaires et dirige l'excédent vers des charges résistives (chauffe-eau, pompe de piscine, etc.) au lieu de la laisser repartir gratuitement sur le réseau.

### Fonctionnalités clés

- **Régulation SSR avancée** — Quatre modes de contrôle : Burst, Cycle Stealing (passage par zéro), Mode Trame (Bresenham) et Contrôle de Phase.
- **Périphériques multiples** — Gestion simultanée d'un équipement à puissance variable (proportionnel) et un équipement tout-ou-rien.
- **Mémoire PSRAM** — Utilisation de l'allocateur PSRAM pour ArduinoJson, permettant le stockage de 365 jours de statistiques sans épuiser la SRAM interne.
- **Persistance NVS** — Les totaux quotidiens sont sauvegardés immédiatement dans les préférences non volatiles ; en cas de coupure de courant ou de redémarrage, aucune donnée n'est perdue.
- **Anticipation météo** — Intégration avec l'API Open-Meteo pour la confiance solaire et les heures de lever/coucher du soleil.
- **Intégration MQTT** — Publie tous les états (puissance réseau, équipements, température SSR) et est compatible Home Assistant via le broker MQTT. Supporte la découverte automatique de dispositifs (auto-discovery).

## Qu'est-ce qu'un routeur PV ?

Un routeur photovoltaïque surveille l'énergie exportée vers le réseau par vos panneaux solaires et dévie cet excédent d'énergie vers une charge résistive (comme un chauffe-eau) au lieu de la laisser repartir gratuitement sur le réseau.

## Stratégies de Routage

Le système gère deux types d'équipements avec des logiques différentes pour maximiser l'autoconsommation :

### Équipement 1 : Puissance Variable (Proportionnel)
Destiné à l'appareil qui doit absorber le surplus de manière fine (ex: chauffe-eau électrique). 
- **Contrôle SSR :** La puissance est ajustée en continu (de 0 à 100%) pour que l'export vers le réseau soit le plus proche de zéro possible.
- **Réactivité :** Ajustement en temps réel basé sur les mesures du compteur d'énergie.

### Équipement 2 : Tout-ou-Rien (On/Off)
Destiné à un second appareil avec une puissance fixe (ex: pompe de piscine, petit radiateur, chargeur).
- **Seuils :** Il s'allume lorsque le surplus solaire dépasse sa puissance nominale et s'éteint si le surplus devient insuffisant.
- **Temporisation :** Inclut des durées minimales de marche/arrêt pour protéger le matériel.

## Anticipation et Confiance Solaire

Grâce à l'intégration de l'API **Open-Meteo**, le routeur ne se contente pas de réagir au soleil présent :
- **Confiance Solaire :** Le système calcule un indice de confiance basé sur les prévisions de nébulosité.
- **Optimisation :** Si la confiance solaire est élevée, le système peut privilégier le démarrage de l'Équipement 2, sachant que la production sera stable.
- **Mode Nuit :** Les heures de lever et coucher du soleil permettent au routeur de passer en mode basse consommation durant la nuit.

### Calibration d'angle de phase (Phase-angle)
Le mode **Contrôle de Phase** ajuste précisément la découpe des demi-ondes du signal AC pour un contrôle fin de la puissance délivrée au SSR. Le routeur dispose d'une tâche dédiée qui calcule et applique les calibrages automatiques via l'interface web (`/phase_cal`).
