# Matériel

Le projet est conçu pour fonctionner sur du matériel moderne et abordable.

## Contrôleur

- **ESP32-S3 :** Recommandé pour ses performances et ses capacités de gestion de l'énergie.
- **ESP32 DevKit :** Également supporté.

## Mesure d'énergie

Le routeur supporte plusieurs types de capteurs pour mesurer l'injection et la consommation :

- **JSY-194G :** Compteur double sens de haute précision (recommandé).
- **JSY-193 :** Alternative compacte.
- **Shelly 1PM :** Supporté via l'API locale pour une intégration sans fil.

## Guide de Câblage

Voici les branchements recommandés pour les cartes de développement courantes.

| Signal | Broche ESP32-S3 | Broche ESP32 Standard | Connexion |
| :--- | :--- | :--- | :--- |
| **Contrôle SSR** | `17` | `22` | Entrée commande du SSR |
| **Relais Sécurité** | `6` | `17` | Bobine du relais (Actif-BAS) |
| **Données 1-Wire** | `16` | `23` | Broche DQ du DS18B20 (+ pull-up 4.7K) |
| **Ventilateur PWM** | `7` | `5` | Entrée PWM du ventilateur (10 kHz) |
| **Zero-Crossing** | `15` | `19` | Sortie du capteur ZX (pull-up 3.3V) |
| **Masse (GND)** | `GND` | `GND` | Commun à tous les capteurs |
| **Alimentation** | `3.3V` / `5V` | `3.3V` / `5V` | Selon les besoins des modules |
| **UART1 (JSY1)** | `4 (TX) / 5 (RX)` | `15 (TX) / 18 (RX)` | Premier compteur JSY-194G |
| **UART2 (JSY2)** | `17 (TX) / 16 (RX)` | `33 (TX) / 32 (RX)` | Second compteur JSY-194G |
| **LED Interne** | `48` | `2` | LED WS2812 embarquée |

> **Note :** La polarité du relais est importante. Par défaut, un signal **BAS (LOW)** ferme le circuit du SSR (fonctionnement normal), tandis qu'un signal **HAUT (HIGH)** l'ouvre (coupure de sécurité).

