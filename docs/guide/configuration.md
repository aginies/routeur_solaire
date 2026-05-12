# Configuration Détaillée

L'interface web permet de configurer tous les aspects du routeur. Voici le détail des options basées sur l'aide intégrée au système.

## Connectivité & Mesure Réseau

### Wi-Fi
- **SSID / Mot de passe** : Identifiants de votre réseau local.
- **IP Fixe** : Vous pouvez configurer une IP statique (IP, Subnet, Gateway, DNS) pour éviter que l'adresse ne change.
- **Mode Point d'Accès** : Si "Utiliser un réseau existant" est à *False*, l'ESP32 crée son propre Wi-Fi (`RouteurSolaire_XXXX`) pour la configuration initiale.

### MQTT & Domotique
Le routeur peut être intégré à votre système domotique (ex: **Home Assistant**) via MQTT.
- **Topic de base** : Par défaut `routeur_solaire`. Tous les messages seront publiés sous cette racine.
- **Topics Publiés** :
    - `status` : `online` ou `offline` (LWT — Last Will and Testament).
    - `power` : Puissance réseau actuelle (W).
    - `eq1_power` : Puissance consommée par l'équipement 1 (W).
    - `eq2_power` : Puissance de l'équipement 2 (W).
    - `eq1_percent` : Pourcentage de puissance envoyé au SSR (%).
    - `ssr_temp` : Température du SSR (°C).
    - `env_temp` : Température ambiante mesurée par le capteur DS18B20 (°C).
    - `fan_active` : État du ventilateur (`ON`/`OFF`).
    - `fan_percent` : Vitesse du ventilateur en pourcentage (%).
    - `eq2_status` : État de l'équipement 2 (ON, OFF, PENDING_ON, etc.).
- **Exemple Home Assistant** :
```yaml
mqtt:
  sensor:
    - name: "Solaire Puissance Réseau"
      state_topic: "routeur_solaire/power"
      unit_of_measurement: "W"
```

### Équipement 2 (Tout-ou-rien)
Pilotage d'un second appareil (pompe de piscine, chargeur, etc.) via un relais ou un Shelly.
- **Priorité** : Définit quel équipement est servi en premier en cas de surplus.
- **Temps min de marche** : Évite les démarrages/arrêts trop fréquents (cycles courts).
- **Planning horaire (48 x 30 min)** : L'équipement 2 peut être activé par un planning pré-configuré ou par le surplus solaire — ou les deux. La page web dédiée affiche une grille de 48 cases cliquables où chaque case représente un créneau de 30 minutes. Les créneaux actifs apparaissent en vert et sont persistés dans `config.json`.

### Planning horaire pour l'équipement 2
Le planning se configure via la page **Équipement 2** de l'interface web (accessible sous `/equip2`). La grille présente les 48 créneaux de 30 minutes sur une période de 24 heures. Les horaires de lever et coucher du soleil sont utilisés pour pré-remplir le planning en fonction des conditions solaires prévues.

### Sources de mesure (Grid Sensor)
Le système supporte deux sources principales pour mesurer l'import/export réseau :
- **Shelly EM / Shelly 1PM** : Mesure via le réseau. Le **Mode MQTT** est recommandé pour une meilleure réactivité (~100ms).
- **JSY-MK-194 (UART)** : Compteur double sens connecté directement en UART, extrêmement fiable et rapide.

### Test de Santé Shelly
L'endpoint API `POST /test_shelly?target=em|eq1|eq2` permet un contrôle à la demande de l'état du dispositif Shelly configuré. Retourne le JSON avec : puissance (`power`), tension (`voltage`) et génération (Gen1 ou Gen2). Utile pour diagnostiquer les problèmes de connectivité.

### Dual JSY — deux compteurs simultanés
Le routeur supporte **deux compteurs JSY-MK-194** connectés sur différents ports UART (UART1 et UART2). Chaque compteur peut être assigné à une mesure différente : l'un pour la puissance réseau (`jsy_grid_channel`) et l'autre pour la consommation de l'équipement 1 (`jsy_equip1_channel`). Cette configuration double-mètre permet un suivi plus précis des flux d'énergie.

---

## Régulation & Modes SSR

La boucle de régulation ajuste la puissance pour atteindre une **Consigne réseau** (généralement 0W pour le "Zéro Export").

### Calibration d'angle de phase (Phase-angle)

Le mode Contrôle de Phase nécessite un calibrage précis des délais. Le routeur dispose d'une interface dédiée (`/web_phase_cal`) et d'un endpoint API `POST /api/phase_cal` :

| Action | Description |
|--------|-------------|
| `start` | Lance une balayage automatique (min/max delay, step size, dwell time). Reboote le routeur pour appliquer. |
| `pause` / `resume` / `jump N` | Contrôle en cours d'exécution. |
| `exit` | Arrête la calibration et reboot. |
| `status` | Retourne l'état : `active`, `paused`, `delay_us`, `gridPower`, `equipPower`, `progress`. |

Les paramètres de calibrage sont configurables :
- `phase_cal_min_us` / `phase_cal_max_us` — Plage de delay (µs).
- `phase_cal_step_us` — Pas de balayage.
- `phase_cal_hold_ms` — Temps d'attente par point de mesure.

> **Note :** Le calibrage peut être automatisé en cochant `phase_calibrate=true`. Après calibration, le routeur redémarre et la propriété est réinitialisée à `false`.

### Modes de contrôle du SSR
Le choix du mode dépend de votre SSR et de votre besoin de précision :

| Mode | Description | SSR requis | Précision |
|------|-------------|------------|-----------|
| **Burst** | Allume/éteint le SSR sur une période fixe (ex: 10s ON, 5s OFF) | Standard | Moyenne |
| **Cycle Stealing** | Change l'état du SSR au passage par zéro (réduit les parasites EMI) | Standard avec ZX | Bonne |
| **Mode Trame (Bresenham)** | Répartition uniforme des cycles ON/OFF via algorithme de Bresenham | Standard avec ZX | Excellente |
| **Contrôle de Phase** | Coupe chaque demi-onde du signal AC (dimmer-like) | SSR « phase aléatoire » + détection Zero-Crossing précise | Très haute |

> **Note :** Le mode Contrôle de Phase nécessite un SSR compatible (type TRIAC à phase aléatoire) et une détection Zero-Crossing précise. Les modes Burst et Cycle Stealing fonctionnent avec la plupart des SSR standards.

---

## Fonctions Spéciales

### Mode Force — Boost Manuel & Forçage Horaires

Le **Mode Force** active la chauffe indépendamment du surplus solaire. Il existe deux variantes :

- **Boost Manuel** — Permet de forcer l'équipement 1 à 100% immédiatement, pour une durée définie (`boost_minutes`) ou jusqu'à ce que la température cible soit atteinte.

- **Forçage Horaires** — Configurez un créneau horaire (ex: `22:05` → `05:55`) pendant lequel le SSR se force à chauffer indépendamment du soleil, utile pour les tarifs heures creuses ou les périodes de forte production prévue.

### Mode Vacances
Le **Mode Vacances** permet de suspendre intelligemment l'activité du routeur lors d'absences prolongées.
- **Fonctionnement** : Lorsqu'il est actif, le routage vers l'équipement 1 et 2 est désactivé.
- **Utilité** : Évite de maintenir un ballon d'eau chaude à température inutilement, réduit l'usure du SSR et économise l'énergie (consommation du ventilateur, etc.).
- **Activation** : Se configure via l'interface web pour un nombre de jours défini ou de manière permanente jusqu'au retour.

### Anticipation Météo
Intégration avec **Open-Meteo** pour :
- Obtenir les heures de lever/coucher du soleil.
- Ajuster le déclenchement de l'équipement 2 selon la "Confiance solaire" prévue.
- Calculer un indice de confiance basé sur les prévisions de nébulosité (0–100 %).

---

## Sécurité & Maintenance

- **Surveillance Température** : Coupure automatique du SSR si la température dépasse le seuil de sécurité.
- **Ventilateur** : Gestion PWM d'un ventilateur de refroidissement pour le SSR.
- **Mises à jour OTA** : Possibilité de téléverser un nouveau fichier `.bin` directement via le navigateur.
- **Logs & Données** : Téléchargement des fichiers `log.txt` (journal des événements) et `solar_data.txt` (statistiques du routeur) pour analyse hors-ligne.

### Limitation de Fréquence (Rate Limiting)
Les opérations de sauvegarde (`save_config`, `save_eq2_schedule`) et les redémarrages sont limités à **une requête toutes les 60 secondes**. Les requêtes trop rapides reçoivent une réponse HTTP 429 :

```json
{"error":"Trop frequent"}
```

Ce mécanisme protège le stockage flash (LittleFS) contre l'usure prématurée et les configurations corrompues par des clics répétés.

### Authentification Web
Le routeur peut exiger une authentification pour accéder aux pages de configuration et aux endpoints API via les champs `web_user` / `web_password`. Par défaut, aucun mot de passe n'est requis. Consultez la page [Sécurité & Maintenance](safety.md) pour plus de détails sur l'authentification web et le mode vacances.
