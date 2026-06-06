---
title: Matériel — Routeur Solaire
description: "Guide matériel du routeur PV : ESP32-S3, capteurs JSY-MK-194 et Shelly EM, schéma de câblage, BOM et configuration PSRAM."
---

# Matériel

Le projet est conçu pour fonctionner sur du matériel moderne et abordable, centré autour de l'ESP32.

## Contrôleur

| Module | Description |
|--------|-------------|
| **ESP32-S3** (recommandé) | Performances optimisées avec PSRAM intégré — idéal pour les statistiques étendues |
| **ESP32 DevKit (WROOM)** | Supporté également, nécessite un SSR standard et une détection Zero-Crossing externe |

## Capteurs d'énergie

Le routeur supporte plusieurs types de capteurs pour mesurer l'injection et la consommation :

- **JSY-MK-194** — Compteur double sens de haute précision. Connecté en UART direct, il offre la meilleure réactivité (~50 ms).
- **JSY-MK-193** — Alternative compacte du même fabricant.
- **Shelly EM / Shelly 1PM** — Mesure WiFi. Le mode MQTT est recommandé pour une réactivité de ~100 ms (sinon l'appel HTTP API ajoute ~500 ms).

## Guide de Câblage

Voici les branchements recommandés pour les cartes de développement courantes. Les broches diffèrent entre ESP32-S3 et ESP32 classique (WROOM/DevKit).

| Signal | Broche ESP32-S3 | Broche ESP32 Standard | Connexion |
| :--- | :---: | :---: | --- |
| **Contrôle SSR** | `17` | `22` | Entrée commande du SSR (actif-HAUT) |
| **Relais Sécurité** | `6` | `17` | Bobine du relais — fermeture au signal BAS |
| **Données 1-Wire** | `16` | `23` | Broche DQ du DS18B20 (pull-up 4,7 kΩ requis) |
| **Ventilateur PWM** | `7` | `5` | Entrée PWM du ventilateur (~10 kHz) |
| **Zero-Crossing** | `15` | `19` | Sortie ZX du capteur (pull-up 3,3 V) |
| **Masse (GND)** | `GND` | `GND` | Commun à tous les capteurs et modules |
| **Alimentation** | `3.3V / 5V` | `3.3V / 5V` | Selon les besoins des modules connectés |
| **UART1 (JSY #1)** | `TX=4, RX=5` | `TX=15, RX=18` | Premier compteur JSY-194G |
| **UART2 (JSY #2)** | `TX=17, RX=16` | `TX=33, RX=32` | Second compteur JSY-194G |
| **LED WS2812** | `48` | `2` | LED interne addressable (NeoPixel) |
| **LCD I2C SDA** | `8` | `8` | Broche SDA pour écran LCD1602A + backpack PCF8574 |
| **LCD I2C SCL** | `9` | `9` | Broche SCL pour écran LCD1602A + backpack PCF8574 |

## Notes importantes

> **Relais de sécurité :** La polarité est importante. Le SSR est piloté en actif-HAUT : le relais de sécurité inverse ce signal pour un comportement *fail-safe*. Un signal **BAS (LOW)** sur IO6 ferme le relais → circuit alimenté (fonctionnement normal). Un signal **HAUT (HIGH)** l'ouvre → coupure du SSR. En cas de coupure ou redémarrage de l'ESP32, la charge est automatiquement déconnectée.

> **DS18B20 :** Le capteur de température 1-Wire nécessite une résistance de pull-up de 4,7 kΩ entre la ligne de données et le 3,3 V. Câblé en mode "parasite", il ne nécessite qu'une seule connexion de données (pas d'alimentation séparée).

> **Zero-Crossing :** Un capteur ZX est requis pour les modes de contrôle SSR avancés (Cycle Stealing, Bresenham). Le signal ZX doit être compatible 3,3 V TTL.

## Configuration PSRAM

| Variante | PSRAM | Taille Flash recommandée |
|----------|-------|-------------------------|
| **N16R8** (ESP32-S3) | 8 Mo SPIRAM + 16 Mo Flash | 16 Mo |
| **N8R2** (ESP32-S3) | 2 Mo SPIRAM + 8 Mo Flash | 8 Mo |

Le PSRAM est utilisé pour stocker les statistiques journalières via ArduinoJson. Avec 8 Mo de PSRAM, le système peut conserver jusqu'à **365 jours** de données détaillées sans épuiser la SRAM interne.

## USB-CDC Natif (ESP32-S3)

L'ESP32-S3 dispose d'un port USB natif sur les broches IO19/IO20, permettant une connexion série directe via `/dev/ttyACMx` (Linux) sans puce USB-UART externe.

### Configuration par défaut

Par défaut, le firmware compile avec `ARDUINO_USB_CDC_ON_BOOT` désactivé (`0`). Le port Serial utilise alors le pont UART0-to-USB-serial (broches IO43/IO44), accessible via `/dev/ttyUSB0`. C'est la configuration recommandée pour les cartes de développement standard.

### Mode USB-CDC natif

Pour activer le mode USB-CDC natif :
1. Dans `platformio.ini`, modifiez l'environnement cible pour définir `-D ARDUINO_USB_CDC_ON_BOOT=1`.
2. Flashage avec la commande `-u` de `flash.sh` (ex: `./flash.sh -u`).

### Comportement au démarrage

Le port USB natif prend environ **1 seconde** à s'énumérer après l'alimentation du module. Sans ce délai, les impressions logiques des premiers millisecondes sont perdues. Le firmware intègre un `delay(200)` en début de setup pour attendre la première partie de l'enumeration.

> **Note :** En mode USB-CDC natif, les broches IO19/IO20 (USB D-/D+) ne sont plus disponibles comme GPIOs standard. Si vous avez besoin de ces pins pour des périphériques externes, restez en mode UART-serial classique.

## Afficheur LCD 1602A (I2C)

Le routeur supporte un afficheur LCD 1602A connecté via un backpack I2C basé sur le circuit PCF8574.

### Branchement

| Signal | Broche ESP32-S3 | Connexion |
| :--- | :---: | --- |
| **VCC** | `3.3V` | Alimentation 3.3 V |
| **GND** | `GND` | Masse commune |
| **SDA** | `IO8` | Ligne de données I2C |
| **SCL** | `IO9` | Horloge I2C |

### Configuration

| Paramètre | Valeur par défaut | Description |
|-----------|-------------------|-------------|
| `e_lcd` | `true` | Active l'afficheur |
| `lcd_sda_pin` | `8` | Broche SDA I2C |
| `lcd_scl_pin` | `9` | Broche SCL I2C |
| `lcd_i2c_addr` | `0x27` (39) | Adresse I2C du backpack PCF8574 |

### Affichage

- **Ligne 1** : Défilement du SSID Wi-Fi et de l'adresse IP (ex: `MonReseau 192.168.1.60`)
- **Ligne 2** : `P:XXXXW R:YYYYW` — `P` = puissance réseau consommée, `R` = puissance redirigée

L'initialisation scanne le bus I2C au démarrage. Si le périphérique n'est pas trouvé, un avertissement est journalisé et l'afficheur reste inactif sans planter le système.

> **Adresse I2C :** La plupart des backpacks PCF8574 utilisent `0x27` (39). Certains utilisent `0x3F` (63). Si l'afficheur ne s'allume pas, essayez `lcd_i2c_addr: 63` dans `config.json`.

## Guide de Construction PCB

Le routeur fonctionne sur un PCB unique qui relie tous les composants (JSY, SSR, relais, ventilateur, capteur). Voici les éléments clés à prendre en compte pour construire ou acheter votre carte.

### Module ESP32-S3

| Module | Description |
|--------|-------------|
| **ESP32-S3-WROOM-1-N16R8** (recommandé) | 41 broches, antenne PCB, PSRAM intégré (8 Mo SPIRAM + 16 Mo Flash). C'est le choix optimal pour les statistiques étendues. |

Le module N16R8 est préféré car il fournit la mémoire suffisante pour ArduinoJson lors de l'analyse des données météo et du stockage des statistiques sur plusieurs jours.

### Composants principaux (BOM)

| Réf. | Composant | Valeur / Type | Qté | Remarques |
|------|-----------|---------------|-----|-----------|
| **U1** | Module ESP32-S3 | ESP32-S3-WROOM-1-N16R8 | 1 | Antenne PCB, broches castelées |
| **U2** | Compteur d'énergie | JSY-MK-194G | 1 | Double canal, Modbus-RTU TTL |
| **U3** | Régulateur LDO | AMS1117-3.3 (SOT-223) | 1 | 800 mA — nécessite C1/C2 = 10 µF |
| **D1** | Diode ESD | PRTR5V0U2X ou USBLC6 | 1 | Protection port USB-C |
| **J1** | Connecteur USB-C | Vertical (GCT USB4135) | 1 | Alimentation + Serial optionnel |
| **J2** | Connecteur JSY | JST-XH 5 pins (2,54 mm) | 1 | VCC, GND, JSY1_TX, JSY1_RX, Zx |
| **J4** | Sortie SSR | Terminal vis 2 pins (5 mm) | 1 | Contrôle SSR (IO17) |
| **J5** | Sortie Relais | Terminal vis 3 pins (5 mm) | 1 | 5 V, GND, Signal (IO6) |
| **J6** | Ventilateur | En-tête 4 pins (2,54 mm) | 1 | 5 V, GND, TACH, PWM (IO7) — 4010 5V PWM recommandé |
| **J7** | Capteur Température | En-tête 3 pins (2,54 mm) | 1 | 3V3, Data (IO16), GND |

### Résistances de strappage (obligatoires)

Les broches **IO0**, **EN** et **IO46** doivent être correctement tirées au démarrage. C'est critique pour le bon fonctionnement du module S3.

| Broche | Fonction | Type de résistance | Valeur | Rôle |
|--------|----------|-------------------|--------|------|
| **IO46** | Strapping mandatory (LOW at boot) | Pull-down | 10 kΩ à GND | Si la broche est HIGH au démarrage, le module ne démarre pas |
| **IO0** | Mode Boot | Pull-up | 10 kΩ → 3V3 | LOW pendant l'alimentation = mode flash (SW1) |
| **EN** | Enable | Pull-up | 10 kΩ → 3V3 | LOW = reset actif (SW2) |

> **Note importante — IO46 :** Cette broche strapping doit être à GND au moment exact de l'alimentation. Placez la résistance R3 physiquement proche du pad IO46 pour éviter que le fil ne capture du bruit et ne tire accidentellement la broche vers HIGH, ce qui empêcherait le démarrage du module.

### Configuration du Relais de Sécurité (J5)

Le relais de sécurité est une seconde couche de protection. Il coupe l'alimentation du SSR en cas de :
1. **Défaut** (surchauffe, capteur déconnecté).
2. **Mode Nuit** (empêche la fuite d'énergie pendant les heures sans soleil).

**Configuration Fail-Safe :**
- Câblez votre charge AC (chauffe-eau) entre les bornes **COM** et **NC (Normally Closed)** du relais.
- **ESP32 actif (normal)** : IO6 est `LOW`, le relais est ouvert → Circuit FERMÉ (alimentation vers SSR).
- **Défaut / Nuit** : IO6 passe à `HIGH`, le relais se ferme → Circuit OUVERT (pas d'alimentation au SSR).

### Connectique UART JSY

L'ESP32-S3 communique avec le compteur JSY-MK-194G via **UART1** (IO4 = TX, IO5 = RX), ce qui laisse les broches USB natives (IO19/IO20) libres pour le mode USB-CDC natif.

| Signal | Broche ESP32-S3 | Fonction |
|--------|-----------------|----------|
| **JSY TX** → ESP RX | `IO4` | Données du compteur vers l'ESP32 |
| **JSY RX** ← ESP TX | `IO5` | Commandes de l'ESP32 au compteur |
| **Zx (Zero-cross)** | `IO15` | Synchronisation passe par zéro |

### Composants de protection

- **Polyfuse F1 (500 mA)** : En série sur la ligne 5 V du port USB. Protège contre les courts-circuits et se réinitialise automatiquement après refroidissement.
- **PRTR5V0U2X (D1)** : Protection ESD sur D+/D− du port USB — empêche l'électricité statique d'atteindre l'ESP32-S3 lors de la connexion/déconnexion.

### Capacités C1–C8

Les condensateurs maintiennent la tension stable et filtrent le bruit haute fréquence :
- **C1, C2 (10 µF)** — Condensateurs de filtrage principaux près du régulateur AMS1117.
- **C3 à C8 (100 nF)** — Capacités de découplage placées le plus près possible de chaque broche VCC du module ESP32-S3 et du JSY-MK-194G.

### Diagramme des connexions principales (ESP32-S3)

```
      [ PÉRIPHÉRIQUES ]             [ BROCHES ESP32-S3 ]          [ ALIMENTATION / SYSTÈME ]
                                ┌─────────────────┐
    ( Compteur d'énergie )       │       GND   [1] │ <────┐ [ Masse commune ]
    JSY TX ───────────(Data In)─│ IO4   3.3V  [2] │ <────┘ [ AMS1117-3.3 OUT ]
    JSY RX ◀───────(Data Out)─→ │ IO5             │           + [ Caps C1-C8 ]
    JSY Zx ──────────(Sync In)->│IO15        IO19 │ ◀───->  USB D- (Serial/DFU)
                                │            IO20 │ ◀───->  USB D+ (Serial/DFU)
    ( Contrôle de charge )       │ [ ACTEURS ]     │
    SSR IN ◀──────────(PWM Out)─│ IO17         EN │ ◀────  Reset [ SW2 + R1 ]
    RELAY  ◀──────────(DRV Out)─│ IO6         IO0 │ ◀────  Boot  [ SW1 + R2 ]
    FAN    <——───────(PWM Out)——│ IO7        IO46 │ ────->  [ R3 Pull-Down ]
                                │                 │
    ( Sens / UI )                │ [UI & SENSE]    │
    TEMP DAT ◀───────(1-Wire)─> │ IO16            │ <────  [ R4 Pull-Up ]
    (WS2812) ◀───────(Status)── │ IO48            │
                                └─────────────────┘
```

### Configuration des panneaux solaires

Le routeur calcule la puissance solaire attendue en utilisant un modèle géométrique de vos panneaux. Ces paramètres sont configurables via l'interface web :

| Paramètre | Description | Plage typique |
|-----------|-------------|---------------|
| **Puissance nominale** (`solar_panel_power`) | Puissance crête des panneaux (Wc) | 50 – 10000 W |
| **Azimut** (`solar_panel_azimuth`) | Orientation face au sud (S=0°, E=90°, O=-90°) | -180° à +180° |
| **Inclinaison** (`solar_panel_tilt`) | Angle du panneau par rapport à l'horizontale (→ verticale) | 0° (plat) – 90° (vertical) |
| **Facteur de perte** (`solar_loss_factor`) | Pertes estimées (poussière, température, vieillissement) | 0.7 – 1.0 |

Ces paramètres sont configurables via l'interface web ([configuration détaillée](./configuration.md)) et utilisés par le gestionnaire météo pour prédire la puissance solaire et optimiser le déclenchement des équipements.

### Liens utiles pour l'achat des composants

Tous les composants sont disponibles sur AliExpress et autres revendeurs d'électronique. Les liens directs figurent dans le fichier [`board/board.md`](../board/board.md) avec :
- **U1 (ESP32-S3)** — [Rechercher ESP32-S3-WROOM-1-N16R8](https://www.aliexpress.com/wholesale?SearchText=ESP32+S3+WROOM+1+N16R8)
- **U2 (JSY-MK-194G)** — [Produit JSY-MK-194G](https://www.aliexpress.com/item/1005006136142011.html)
- **U3 (AMS1117-3.3 LDO)** — [Produit AMS1117 SOT-223](https://www.aliexpress.com/item/32832675358.html)
- **D1 (ESD PRTR5V0U2X)** — [Rechercher PRTR5V0U2X](https://www.aliexpress.com/wholesale?SearchText=PRTR5V0U2X)
- **J1 (USB-C vertical)** — [Rechercher USB-C Vertical SMD](https://www.aliexpress.com/wholesale?SearchText=USB+C+Vertical+SMD)
- **J4/J5 (Terminaux vis 5,08 mm)** — [Produit Terminal vis 2-pin](https://www.aliexpress.com/item/32845661413.html)
- **J6 (Ventilateur 4010 5V PWM)** — [Rechercher 4010 5V PWM](https://www.aliexpress.com/wholesale?SearchText=4010+5v+pwm+fan) / [Noctua NF-A4x20 5V PWM](https://www.aliexpress.com/item/1005001936813874.html)
- **Relais 5 V** — [Produit Relais 1 canal](https://www.aliexpress.com/item/32858302901.html)
- **J2/J7 (Connecteurs JST-XH)** — [Rechercher JST-XH 2.54mm](https://www.aliexpress.com/wholesale?SearchText=JST+XH+2.54+header)
- **SW1/SW2 (Boutons tactiles SMD 6×6 mm)** — [Produit Tactile SMD Button](https://www.aliexpress.com/item/32858302188.html)
- **Passifs** — [Kit condensateurs + résistances 0402](https://www.aliexpress.com/wholesale?SearchText=0402+capacitor+resistor+assortment)

> Les liens "Rechercher" utilisent les paramètres de recherche directs d'AliExpress et restent stables même si le site change sa structure. Les liens "Produit" pointent vers des fiches articles spécifiques — si un article n'est plus disponible, remplacez simplement l'ID dans la URL (ex: `item/32845661413.html`).

