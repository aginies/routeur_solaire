# Captures d'écran

## Interface Principale

![Interface Principale](/main.png)

Vue d'ensemble de la production solaire et du routage en cours. Affiche la puissance réseau (import/export), le pourcentage SSR, les températures et l'état des équipements.

---

## Statistiques

![Statistiques](/stats.png)

Suivi détaillé des économies réalisées :
- **Totaux quotidiens** — Import, redirection et export d'aujourd'hui.
- **Graphiques horaires** — Puissance par heure de la journée (import, redirection, export).
- **Historique** — Statistiques des 30 derniers jours (configurable via `MAX_STATS_DAYS`).

---

## Configuration

![Configuration](/config.png)

Réglages des paramètres du routeur :
- **Connectivité** — Wi-Fi, MQTT, IP statique.
- **Capteurs** — Sources de mesure (Shelly EM / JSY-MK-194).
- **Équipements** — Puissances, seuils et modes SSR.
- **Sécurité** — Seuils de température, gestion du ventilateur.

---

## Outils Développeur

![Développeur](/dev.png)

Outils de diagnostic et monitoring en temps réel :
- État des capteurs et actionneurs (SSR, relais, ventilateur).
- Puissance réseau mesurée vs estimée.
- Journal d'événements (logs) accessible via téléchargement.

Pour plus de détails sur les endpoints API (`/status`, `/test_shelly`, etc.), consultez la page [API & Diagnostics](diagnostics.md).

---

## Aperçu du système

L'interface principale offre une vue synthétique du système avec les indicateurs clés de performance et l'état global du routeur (voir Interface Principale ci-dessus).
