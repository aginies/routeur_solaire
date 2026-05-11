# Introduction

Le **Routeur Solaire** est une implémentation C++ haute performance d'un routeur photovoltaïque, optimisée pour la plateforme ESP32.

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
