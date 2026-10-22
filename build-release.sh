#!/bin/bash
# AZELCtrl Release Builder
# Builds the firmware, packages all files into a release zip
# Usage: ./build-release.sh [version-tag]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RELEASE_DIR="$SCRIPT_DIR/release"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
VERSION="${1:-v1.5.2}"
ZIP_NAME="azelctrl-${VERSION}.zip"

echo "============================================"
echo " AZELCtrl Release Builder — ${VERSION}"
echo "============================================"

# ------------------------------------------------------------------
# Step 1: Compress web interface and generate PROGMEM fallback header
# ------------------------------------------------------------------
echo ""
echo "[1/4] Compressing web interface..."
cd "$SCRIPT_DIR/satellite-tracker"

gzip -9 -c index.html > index.html.gz
echo "  -> index.html.gz ($(ls -lh index.html.gz | awk '{print $5}'))"

echo "[2/4] Generating PROGMEM fallback header..."
{
  echo "// Auto-generated — do not edit manually"
  echo "// Source: index.html"
  echo "// Compressed with gzip -9"
  echo ""
  echo "const uint8_t index_html_gz[] PROGMEM = {"
  gzip -9 -c index.html | xxd -i -
  echo "};"
  echo "const size_t index_html_gz_len = sizeof(index_html_gz);"
} > index_gz.h
cp index_gz.h "$SCRIPT_DIR/ycint/"
echo "  -> index_gz.h"

# ------------------------------------------------------------------
# Step 2: Compile firmware
# ------------------------------------------------------------------
echo ""
echo "[3/4] Compiling firmware..."
cd "$SCRIPT_DIR/ycint"
mkdir -p build

arduino-cli compile \
  --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M \
  --output-dir build \
  .

FIRMWARE_BIN="build/ycint.ino.bin"
if [ ! -f "$FIRMWARE_BIN" ]; then
  echo "ERROR: Firmware compilation failed — $FIRMWARE_BIN not found"
  exit 1
fi
echo "  -> $FIRMWARE_BIN ($(ls -lh "$FIRMWARE_BIN" | awk '{print $5}'))"

# ------------------------------------------------------------------
# Step 4: Assemble release zip
# ------------------------------------------------------------------
echo ""
echo "[4/4] Assembling release package..."
mkdir -p "$RELEASE_DIR"

cp "$FIRMWARE_BIN" "$RELEASE_DIR/azelctrl-firmware.bin"
cp "$SCRIPT_DIR/satellite-tracker/index.html.gz" "$RELEASE_DIR/"
cp "$SCRIPT_DIR/satellite-tracker/favicon.ico" "$RELEASE_DIR/"
cp "$SCRIPT_DIR/satellite-tracker/world_coastline.js.gz" "$RELEASE_DIR/"

cd "$RELEASE_DIR"
zip -j "$SCRIPT_DIR/$ZIP_NAME" \
  azelctrl-firmware.bin \
  index.html.gz \
  favicon.ico \
  world_coastline.js.gz

rm -rf "$RELEASE_DIR"

echo ""
echo "============================================"
echo " Release package created:"
echo "   $SCRIPT_DIR/$ZIP_NAME"
echo ""
echo " Contents:"
unzip -l "$SCRIPT_DIR/$ZIP_NAME" 2>&1 | tail -n +4
echo "============================================"