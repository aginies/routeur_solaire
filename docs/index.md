---
layout: home

hero:
  name: "Routeur Solaire"
  image:
    src: /solar-icon.svg
    alt: "Routeur Solaire"
  text: "Optimisez votre Autoconsommation"
  tagline: "Firmware libre pour ESP32 — routage photovoltaïque haute performance"
  actions:
    - theme: brand
      text: Commencer →
      link: /guide/quick-start
    - theme: alt
      text: Voir sur GitHub
      link: https://github.com/aginies/routeur_solaire

---

## Architecture Système

![Architecture Système](/system-arch.svg)

## Architecture Carte

![Architecture Carte](/board-arch.svg)

## Fonctionnalités

<div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1.5rem; margin-top: 2rem;">

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🔌</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Routage Proportionnel</div>
<div style="font-size: 0.9rem; color: #475569;">Ajustement dynamique (0-100%) vers votre chauffe-eau via SSR avec mode Boost Manuel et gestion des Vacances.</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">⚡</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Pilotage Intelligent</div>
<div style="font-size: 0.9rem; color: #475569;">Pilotez un deuxième appareil (Shelly/relais) avec priorité configurable (chauffe-eau ou PAC en premier) et seuils de puissance.</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">☁️</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Anticipation Météo</div>
<div style="font-size: 0.9rem; color: #475569;">Calcul de la "Confiance Solaire" via Open-Meteo pour optimiser le déclenchement selon les prévisions de nébulosité et rayonnement.</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🏠</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Intégration Domotique</div>
<div style="font-size: 0.9rem; color: #475569;">Support complet MQTT pour Home Assistant (Discovery automatique, monitoring temps réel et contrôle à distance).</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">📊</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Dashboard Interactif</div>
<div style="font-size: 0.9rem; color: #475569;">Visualisation ultra-réactive (uPlot.js), statistiques d'économies et configuration web complète avec interface de calibration.</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🛡️</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Sécurité & Fiabilité</div>
<div style="font-size: 0.9rem; color: #475569;">Machine à états avec priorités, surveillance thermique avec hystérésis 5°C, ventilateur PWM, watchdogs et coupures d'urgence.</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🔧</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Contrôles SSR Avancés</div>
<div style="font-size: 0.9rem; color: #475569;">Quatre modes de régulation : Burst (PWM lent), Cycle Stealing (passage par zéro), Mode Trame (Bresenham) et Contrôle de Phase (TRIAC).</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">📈</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Statistiques Étendues</div>
<div style="font-size: 0.9rem; color: #475569;">365 jours de données stockées en PSRAM. Historique horaire, totaux quotidiens et import/export avec persistance NVS anti-coupure.</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🖥️</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Double Support Matériel</div>
<div style="font-size: 0.9rem; color: #475569;">ESP32-S3 (PSRAM, 365 jours de stats) et ESP32-WROOM (version allégée sans stats). Build automatique via GitHub Actions.</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🎯</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Calibrage Automatique</div>
<div style="font-size: 0.9rem; color: #475569;">Wizard intégré pour calibrage de l'angle de phase : balayage automatique, pause/reprise, sauvegarde dans config.json.</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🔩</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Conception PCB Libre</div>
<div style="font-size: 0.9rem; color: #475569;">Projet KiCad complet avec BOM, schémas, netlist et liens d'achat. Custom mainboard pour JSY, SSR, relais, ventilateur.</div>
</div>

<div style="background: #f8fafc; border-radius: 12px; padding: 1.5rem; border: 1px solid #e2e8f0;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🧪</div>
<div style="font-weight: 600; margin-bottom: 0.5rem;">Tests & CI</div>
<div style="font-size: 0.9rem; color: #475569;">Tests unitaires natifs (température, sécurité, contrôleur, statistiques) et builds automatiques esp32s3/esp32dev sur tag push.</div>
</div>

</div>
