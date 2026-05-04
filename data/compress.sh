#!/bin/bash
# Compress static assets for LittleFS
cd "$(dirname "$0")"

FILES="web_command.html web_config.html web_stats.html web_equip2.html help.json style.css main.js"

echo "--- Compressing static assets ---"
for f in $FILES; do
    if [ -f "$f" ]; then
        echo "Compressing $f..."
        gzip -c "$f" > "$f.gz"
        ORIG_SIZE=$(stat -c%s "$f")
        GZ_SIZE=$(stat -c%s "$f.gz")
        echo "  $ORIG_SIZE -> $GZ_SIZE bytes"
    fi
done
echo "--- Done ---"
