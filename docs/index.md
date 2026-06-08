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

---

## Fonctionnalités

<div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1.5rem; margin-top: 2rem;">

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🔌</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Routage Proportionnel</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Ajustement dynamique (0-100%) vers votre chauffe-eau via SSR avec mode Boost Manuel et gestion des Vacances.</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">⚡</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Pilotage Intelligent</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Pilotez un deuxième appareil (Shelly/relais) avec priorité configurable (chauffe-eau ou PAC en premier) et seuils de puissance.</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">☁️</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Anticipation Météo</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Calcul de la "Confiance Solaire" via Open-Meteo pour optimiser le déclenchement selon les prévisions de nébulosité et rayonnement.</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🏠</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Intégration Domotique</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Support complet MQTT pour Home Assistant (Discovery automatique, monitoring temps réel et contrôle à distance).</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">📊</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Dashboard Interactif</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Visualisation ultra-réactive (uPlot.js), statistiques d'économies et configuration web complète avec interface de calibration.</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🛡️</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Sécurité & Fiabilité</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Machine à états avec priorités, surveillance thermique avec hystérésis 5°C, ventilateur PWM, watchdogs et coupures d'urgence.</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🔧</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Contrôles SSR Avancés</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Quatre modes de régulation : Burst (PWM lent), Cycle Stealing (passage par zéro), Mode Trame (Bresenham) et Contrôle de Phase (TRIAC).</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">📈</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Statistiques Étendues</div>
<div style="font-size: 0.9rem; color: #94a3b8;">365 jours de données stockées en PSRAM. Historique horaire, totaux quotidiens et import/export avec persistance NVS anti-coupure.</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🖥️</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Double Support Matériel</div>
<div style="font-size: 0.9rem; color: #94a3b8;">ESP32-S3 (PSRAM, 365 jours de stats) et ESP32-WROOM (version allégée sans stats). Build automatique via GitHub Actions.</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🎯</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Calibrage Automatique</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Wizard intégré pour calibrage de l'angle de phase : balayage automatique, pause/reprise, sauvegarde dans config.json.</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🔩</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Conception PCB Libre</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Projet KiCad complet avec BOM, schémas, netlist et liens d'achat. Custom mainboard pour JSY, SSR, relais, ventilateur.</div>
</div>

<div style="background: #1e293b; border-radius: 12px; padding: 1.5rem; border: 1px solid #334155;">
<div style="font-size: 1.5rem; margin-bottom: 0.5rem;">🧪</div>
<div style="font-weight: 600; margin-bottom: 0.5rem; color: #f1f5f9;">Tests & CI</div>
<div style="font-size: 0.9rem; color: #94a3b8;">Tests unitaires natifs (température, sécurité, contrôleur, statistiques) et builds automatiques esp32s3/esp32dev sur tag push.</div>
</div>

</div>

---

## Architecture Carte

Vue bloc de la PCB principale : alimentation, ESP32-S3, et tous les périphériques connectés.

![Architecture Carte](/board-arch.svg)
