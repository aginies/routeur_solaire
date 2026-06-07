---
layout: home

hero:
  name: "Routeur Solaire"
  text: "Optimisez votre Autoconsommation"
  tagline: "Firmware libre pour ESP32 — routage photovoltaïque haute performance"
  actions:
    - theme: brand
      text: Commencer →
      link: /guide/quick-start
    - theme: alt
      text: Voir sur GitHub
      link: https://github.com/aginies/routeur_solaire

features:
  - title: "Routage Proportionnel"
    icon: 🔌
    details: "Ajustement dynamique (0-100%) vers votre chauffe-eau via SSR avec mode Boost Manuel et gestion des Vacances."
  - title: "Pilotage Intelligent"
    icon: ⚡
    details: "Pilotez un deuxième appareil (Shelly/relais) avec priorité configurable (chauffe-eau ou PAC en premier) et seuils de puissance."
  - title: "Anticipation Météo"
    icon: ☁️
    details: "Calcul de la 'Confiance Solaire' via Open-Meteo pour optimiser le déclenchement selon les prévisions de nébulosité et rayonnement."
  - title: "Intégration Domotique"
    icon: 🏠
    details: "Support complet MQTT pour Home Assistant (Discovery automatique, monitoring temps réel et contrôle à distance)."
  - title: "Dashboard Interactif"
    icon: 📊
    details: "Visualisation ultra-réactive (uPlot.js), statistiques d'économies et configuration web complète avec interface de calibration."
  - title: "Sécurité & Fiabilité"
    icon: 🛡️
    details: "Machine à états avec priorités, surveillance thermique avec hystérésis 5°C, ventilateur PWM, watchdogs et coupures d'urgence."
  - title: "Contrôles SSR Avancés"
    icon: 🔧
    details: "Quatre modes de régulation : Burst (PWM lent), Cycle Stealing (passage par zéro), Mode Trame (Bresenham) et Contrôle de Phase (TRIAC)."
  - title: "Statistiques Étendues"
    icon: 📈
    details: "365 jours de données stockées en PSRAM. Historique horaire, totaux quotidiens et import/export avec persistance NVS anti-coupure."
  - title: "Double Support Matériel"
    icon: 🖥️
    details: "ESP32-S3 (PSRAM, 365 jours de stats) et ESP32-WROOM (version allégée sans stats). Build automatique via GitHub Actions."
  - title: "Calibrage Automatique"
    icon: 🎯
    details: "Wizard intégré pour calibrage de l'angle de phase : balayage automatique, pause/reprise, sauvegarde dans config.json."
  - title: "Conception PCB Libre"
    icon: 🔩
    details: "Projet KiCad complet avec BOM, schémas, netlist et liens d'achat. Custom mainboard pour JSY, SSR, relais, ventilateur."
  - title: "Tests & CI"
    icon: 🧪
    details: "Tests unitaires natifs (température, sécurité, contrôleur, statistiques) et builds automatiques esp32s3/esp32dev sur tag push."
---
