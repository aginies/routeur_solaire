#!/bin/bash
# Exit on any error
set -e

# Default values
ENV="s3"
SKIP_FS=false
STATS_DAYS=""
RUN_TESTS=false
MONITOR=false
ERASE=false

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -e <env>    Target environment: 's3' (default), 'wroom' (mini kit), or 'native' (for tests)"
    echo "  -d <days>   Override MAX_STATS_DAYS (history limit)"
    echo "  -t, --test  Run unit tests on host (native environment)"
    echo "  -m, --monitor Launch serial monitor after flashing"
    echo "  --erase     Full chip erase (clears NVS/Stats) before upload"
    echo "  --skip-fs   Skip building and uploading filesystem"
    echo "  -h, --help  Show this help message"
}

# Cleanup function to restore config if swapped
cleanup() {
    if [ -f "data/config.json.bak" ]; then
        echo "--- Restoring original config.json ---"
        mv data/config.json.bak data/config.json
    fi
}
trap cleanup EXIT

# Parse options
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -e) 
            if [[ -n "$2" && "$2" != -* ]]; then
                ENV="$2"; shift
            else
                echo "Error: Argument for $1 is missing" >&2
                exit 1
            fi
            ;;
        -d) 
            if [[ -n "$2" && "$2" != -* ]]; then
                STATS_DAYS="$2"; shift
            else
                echo "Error: Argument for $1 is missing" >&2
                exit 1
            fi
            ;;
        -t|--test) RUN_TESTS=true ;;
        -m|--monitor) MONITOR=true ;;
        --erase) ERASE=true ;;
        --skip-fs) SKIP_FS=true ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown parameter passed: $1"; usage; exit 1 ;;
    esac
    shift
done

# 1. Handle Unit Tests if requested
if [ "$RUN_TESTS" = true ]; then
    echo "--- RUNNING UNIT TESTS (NATIVE) ---"
    pio test -e native
    echo "--- TESTS PASSED ---"
    # If we only wanted tests, we can exit here
    if [ "$ENV" == "s3" ] && [ "$SKIP_FS" == false ] && [ "$COMPRESS" == false ] && [ -z "$STATS_DAYS" ]; then
         # Check if user passed other arguments. If not, exit.
         # Actually simpler: if they didn't specify an env, assume they only wanted tests.
         # But the user might want tests then flash.
         echo "Tests finished."
    fi
fi

# Map shortcut to full pio env name
if [ "$ENV" == "s3" ]; then
    PIO_ENV="esp32s3"
    echo "--- TARGET: ESP32-S3 ---"
elif [ "$ENV" == "wroom" ]; then
    PIO_ENV="esp32dev"
    echo "--- TARGET: ESP32 WROOM (Mini Kit) ---"
    
    # Check if we should swap config for WROOM
    if [ -f "data/config_wroom.json" ]; then
        echo "--- Swapping config.json with config_wroom.json ---"
        [ -f "data/config.json" ] && mv data/config.json data/config.json.bak
        cp data/config_wroom.json data/config.json
    fi
elif [ "$ENV" == "native" ]; then
    echo "Environment 'native' selected. Running tests only."
    pio test -e native
    exit 0
else
    echo "Error: Invalid environment '$ENV'. Use 's3', 'wroom', or 'native'."
    exit 1
fi

# Handle Stats Days override
if [ -n "$STATS_DAYS" ]; then
    export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -D MAX_STATS_DAYS=$STATS_DAYS"
    echo "--- OVERRIDE: MAX_STATS_DAYS=$STATS_DAYS ---"
fi

echo "--- 1. Cleaning project ---"
pio run -e $PIO_ENV -t clean

if [ "$ERASE" = true ]; then
    echo "--- 1b. Erasing Full Chip (NVS/Flash) ---"
    pio run -e $PIO_ENV -t erase
fi

echo "--- 2. Building and Uploading Firmware ($PIO_ENV) ---"
pio run -e $PIO_ENV -t upload

if [ "$SKIP_FS" = false ]; then
    echo "--- 3a. Compressing HTML assets ---"
    bash data/compress.sh
    echo "--- 3b. Building and Uploading Filesystem (LittleFS) ---"
    pio run -e $PIO_ENV -t uploadfs
fi

echo "--- DONE! ---"
if [ "$MONITOR" = true ]; then
    echo "Starting Serial Monitor (Press Ctrl+C to stop)..."
    sleep 2
    pio device monitor -e $PIO_ENV --filter time --filter colorize --filter debug
fi
