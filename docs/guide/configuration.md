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
    - `status` : `online` ou `offline` (LWT).
    - `power` : Puissance réseau actuelle (W).
    - `eq1_power` : Puissance consommée par l'équipement 1 (W).
    - `eq1_percent` : Pourcentage de puissance envoyée au SSR (%).
    - `ssr_temp` : Température du SSR (°C).
    - `fan_active` : État du ventilateur (`ON`/`OFF`).
- **Exemple Home Assistant** :
```yaml
mqtt:
  sensor:
    - name: "Solaire Puissance Réseau"
      state_topic: "routeur_solaire/power"
      unit_of_measurement: "W"
```

### Sources de mesure (Grid Sensor)
Le système supporte deux sources principales pour mesurer l'import/export réseau :
- **Shelly EM (WiFi)** : Mesure via le réseau. Le **Mode MQTT** est recommandé pour une meilleure réactivité (~100ms).
- **JSY-MK-194 (UART)** : Connexion filaire directe, extrêmement fiable et rapide.

---

## Gestion des Équipements

### Équipement 1 (Régulation Proportionnelle)
C'est l'appareil principal (généralement un chauffe-eau) piloté par le SSR.
- **Puissance max (W)** : Puissance nominale de votre résistance.
- **Puissance max redirigée (%)** : Permet de brider la puissance maximale envoyée (ex: 80% pour limiter la chauffe).
- **Source mesure Eq1** : Vous pouvez déporter la mesure de consommation de cet équipement via un Shelly ou un canal JSY dédié.

### Équipement 2 (Tout-ou-rien)
Pilotage d'un second appareil (pompe de piscine, chargeur, etc.) via un relais ou un Shelly.
- **Priorité** : Définit quel équipement est servi en premier en cas de surplus.
- **Temps min de marche** : Évite les démarrages/arrêts trop fréquents (cycles courts).

---

## Régulation & Modes SSR

La boucle de régulation ajuste la puissance pour atteindre une **Consigne réseau** (généralement 0W pour le "Zéro Export").

### Modes de contrôle du SSR
Le choix du mode dépend de votre SSR et de votre besoin de précision :
- **Burst** : Allume/éteint le SSR sur une période fixe.
- **Cycle Stealing** : Allume/éteint au passage par zéro (réduit les parasites).
- **Mode Trame (Bresenham)** : Répartition uniforme des cycles ON/OFF.
- **Contrôle de Phase** : Coupe chaque demi-onde. **Nécessite un SSR "Random Phase"** et une détection Zero-Cross précise.

---

## Fonctions Spéciales

### Mode Force & Boost Manuel
- **Boost Manuel** : Permet de forcer l'équipement 1 à 100% de sa puissance immédiatement. Utile pour un besoin ponctuel d'eau chaude ou pour un cycle anti-légionelle. Le boost peut être activé pour une durée définie ou jusqu'à ce que la température cible soit atteinte.
- **Plage Force Horizontale** : Vous pouvez configurer une plage horaire régulière (ex: tarif nuit) pour forcer la chauffe indépendamment du soleil.

### Mode Vacances
Le **Mode Vacances** permet de suspendre intelligemment l'activité du routeur lors d'absences prolongées.
- **Fonctionnement** : Lorsqu'il est actif, le routage vers l'équipement 1 et 2 est désactivé.
- **Utilité** : Évite de maintenir un ballon d'eau chaude à température inutilement, réduit l'usure du SSR et économise l'énergie (consommation du ventilateur, etc.).
- **Activation** : Se configure via l'interface web pour un nombre de jours défini ou de manière permanente jusqu'au retour.

### Anticipation Météo
Intégration avec **Open-Meteo** pour :
- Obtenir les heures de lever/coucher du soleil.
- Ajuster le déclenchement de l'équipement 2 selon la "Confiance solaire" prévue.

---

## Sécurité & Maintenance

- **Surveillance Température** : Coupure automatique du SSR si la température dépasse le seuil de sécurité.
- **Ventilateur** : Gestion PWM d'un ventilateur de refroidissement pour le SSR.
- **Mises à jour OTA** : Possibilité de téléverser un nouveau fichier `.bin` directement via le navigateur.
- **Logs & Données** : Téléchargement des fichiers `log.txt` et `solar_data.txt` pour analyse.
