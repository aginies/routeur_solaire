#!/bin/bash
# Exit on any error
set -e

# Default values
ENV="s3"
SKIP_FS=false
STATS_DAYS=""
RUN_TESTS=false

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -e <env>    Target environment: 's3' (default), 'wroom' (mini kit), or 'native' (for tests)"
    echo "  -d <days>   Override MAX_STATS_DAYS (history limit)"
    echo "  -t, --test  Run unit tests on host (native environment)"
    echo "  --skip-fs   Skip building and uploading filesystem"
    echo "  -h, --help  Show this help message"
}

# Parse options
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -e) ENV="$2"; shift ;;
        -d) STATS_DAYS="$2"; shift ;;
        -t|--test) RUN_TESTS=true ;;
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
    if [ "$#" -eq 0 ] && [ "$ENV" == "s3" ]; then
        echo "Exiting after tests. To flash, run without --test or specify an environment."
        exit 0
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
    export PLATFORMIO_BUILD_FLAGS="-D MAX_STATS_DAYS=$STATS_DAYS"
    echo "--- OVERRIDE: MAX_STATS_DAYS=$STATS_DAYS ---"
fi

echo "--- 1. Cleaning project ---"
pio run -e $PIO_ENV -t clean

echo "--- 2. Building and Uploading Firmware ($PIO_ENV) ---"
pio run -e $PIO_ENV -t upload

if [ "$SKIP_FS" = false ]; then
    echo "--- 3. Building and Uploading Filesystem (LittleFS) ---"
    pio run -e $PIO_ENV -t uploadfs
fi

# Cleanup swapped config if we did it
if [ "$ENV" == "wroom" ] && [ -f "data/config.json.bak" ]; then
    echo "--- Restoring original config.json ---"
    mv data/config.json.bak data/config.json
fi

echo "--- DONE! ---"
echo "Starting Serial Monitor (Press Ctrl+C to stop)..."
sleep 2
pio device monitor -e $PIO_ENV --filter time --filter colorize --filter debug
