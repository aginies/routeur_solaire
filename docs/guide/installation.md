# Installation

Cette page explique comment compiler et installer le firmware sur votre ESP32 en utilisant les outils fournis.

## Prérequis

- **Linux / macOS :** Le script d'installation est conçu pour les environnements Bash.
- **PlatformIO CLI :** Assurez-vous d'avoir `pio` installé dans votre terminal.
- **Dépendances Python :** Requis par PlatformIO pour la compilation.

## Méthode Recommandée : Script `flash.sh`

Le projet inclut un script `flash.sh` qui automatise le nettoyage, la compilation, la compression des fichiers web et le flashage du firmware et du système de fichiers (LittleFS).

### Utilisation de base

Pour un ESP32-S3 standard :
```bash
./flash.sh
```

### Options avancées

Le script supporte plusieurs options pour s'adapter à votre matériel :

- `-e <env>` : Cible l'environnement (`s3` par défaut, ou `wroom` pour les modules classiques).
- `-v <variant>` : Spécifie la variante matérielle de l'ESP32-S3 (ex: `N16R8`, `N8R2`). Cela configure automatiquement la taille de la flash et de la PSRAM.
- `-m` : Lance le moniteur série immédiatement après le flashage.
- `-u` : Active le support USB-CDC natif (plus rapide pour le flashage sur S3).
- `--erase` : Efface complètement la puce (NVS et statistiques) avant de flasher.
- `--skip-fs` : Ne flashe que le firmware (saute l'étape LittleFS).

**Exemple pour un module N16R8 avec moniteur série :**
```bash
./flash.sh -v N16R8 -m
```

## Méthode Manuelle (PlatformIO)

Si vous préférez utiliser PlatformIO directement :

1. **Compiler et flasher le firmware :**
   ```bash
   pio run -t upload
   ```

2. **Compresser les assets web (Optionnel mais recommandé) :**
   ```bash
   cd data && ./compress.sh && cd ..
   ```

3. **Flasher le système de fichiers (LittleFS) :**
   ```bash
   pio run -t uploadfs
   ```

## Premier démarrage

Au premier démarrage, si aucun Wi-Fi n'est configuré, l'ESP32 créera un point d'accès nommé `RouteurSolaire_XXXX`. Connectez-vous à ce réseau pour accéder à l'interface de configuration initiale.
