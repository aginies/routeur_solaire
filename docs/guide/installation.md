# Installation

Cette page explique comment compiler et installer le firmware sur votre ESP32 en utilisant les outils fournis.

## Vue d'ensemble

Le projet utilise PlatformIO pour la compilation. Un script `flash.sh` est fourni pour automatiser l'installation courante, mais vous pouvez aussi utiliser PlatformIO directement.

## Prérequis

- **Linux / macOS** : Le script d'installation est conçu pour les environnements Bash.
- **PlatformIO CLI (`pio`)** : Assurez-vous d'avoir `pio` installé dans votre terminal (`pip install platformio`).
- **Dépendances Python** : Requises par PlatformIO (généralement installées automatiquement).

## Méthode Recommandée : Script `flash.sh`

Le projet inclut un script `flash.sh` qui automatise le nettoyage, la compilation, la compression des fichiers web et le flashage du firmware et du système de fichiers (LittleFS).

### Utilisation de base

Pour un ESP32-S3 standard :
```bash
./flash.sh
```

### Options avancées

Le script supporte plusieurs options pour s'adapter à votre matériel :

| Option | Description |
|--------|-------------|
| `-e <env>` | Environnement cible (`s3` par défaut, `wroom` pour ESP32 classique) |
| `-v <variant>` | Variante matérielle (ex: `N16R8`, `N8R2`) — configure automatiquement flash et PSRAM |
| `-m` | Lance le moniteur série après le flashage |
| `-u` | Active le support USB-CDC natif (flash plus rapide sur ESP32-S3) |
| `-t` / `--test` | Exécute les tests unitaires (`pio test`) avant de flasher |
| `--erase` | Efface la puce complète (NVS + statistiques) avant de flasher |
| `--skip-fs` | Flashe uniquement le firmware (saute l'étape LittleFS) |

> **Note :** L'environnement WROOM (`-e wroom`) active automatiquement un fichier `config.json` différent adapté aux broches de l'ESP32-WROOM. Le fichier original est préservé sous `.bak`.

### Variantes matérielles courantes

| Variante | PSRAM | Flash | Quand l'utiliser ? |
|----------|-------|-------|--------------------|
| **N16R8** | 8 Mo SPIRAM | 16 Mo | Modules ESP32-S3 avec grande mémoire (recommandé) |
| **N8R2** | 2 Mo SPIRAM | 8 Mo | Modules plus compacts |

**Exemple pour un module N16R8 avec moniteur série :**
```bash
./flash.sh -v N16R8 -m
```

## Méthode Manuelle (PlatformIO)

Si vous préférez utiliser PlatformIO directement :

### 1. Compiler et flasher le firmware
```bash
pio run -t upload
```

### 2. Compresser les assets web (optionnel mais recommandé)
Les fichiers HTML/CSS/JS sont compressés avec gzip pour réduire l'utilisation du stockage Flash (LittleFS).
```bash
cd data && ./compress.sh && cd ..
```

### 3. Flascher le système de fichiers (LittleFS)
```bash
pio run -t uploadfs
```

## Compilation croisée (Native Test)

Pour exécuter les tests unitaires sur votre machine hôte (Linux/macOS) sans ESP32 :

```bash
# Compiler pour le host
pio run --environment native

# Lancer les tests
./build/native
```

Le mode natif utilise des stubs (`NATIVE_TEST`) pour Arduino, LittleFS et les primitives FreeRTOS. Idéal pour développer rapidement avec `gdb` ou `valgrind`.

## Premier démarrage

Au premier démarrage, si aucun Wi-Fi n'est configuré (ou si le réseau est introuvable), l'ESP32 créera un point d'accès nommé `RouteurSolaire_XXXX`. Connectez-vous à ce réseau et ouvrez `http://192.168.4.1` pour accéder à l'interface de configuration initiale.

## Dépannage rapide

- **LED interne fixe** : L'ESP32 est probablement en AP mode, attendant une connexion Wi-Fi.
- **Pas d'accès Web** : Vérifiez que vous êtes bien sur le réseau `RouteurSolaire_XXXX` ou dans le même sous-réseau WiFi.
- **Erreurs LittleFS** : Essayez `./flash.sh --erase` pour réinitialiser la mémoire non volatile.
- **PSRAM non détectée** : Vérifiez que votre variante matérielle correspond au module (`N16R8` ou `N8R2`).
