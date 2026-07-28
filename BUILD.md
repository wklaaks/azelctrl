# AZELCtrl — Build & Flash Guide

This document explains how to build the satellite tracker firmware and flash it to the ESP8266.

## File Locations

| File | Path | Description |
|------|------|-------------|
| **HTML source** | `~/projects/azelctrl/satellite-tracker/index.html` | Satellite tracker UI (203 KB) — edit this file for UI changes |
| **Compressed HTML** | `~/projects/azelctrl/satellite-tracker/index.html.gz` | Gzip-compressed HTML (46 KB) — upload to ESP via `/upload` endpoint |
| **Firmware source** | `~/projects/azelctrl/ycint/ycint.ino` | ESP8266 Arduino sketch — edit for firmware changes |
| **Fallback HTML** | `~/projects/azelctrl/ycint/index_gz.h` | Auto-generated C header — PROGMEM fallback if SPIFFS has no file |
| **ESP firmware** | `~/projects/azelctrl/ycint/` | Arduino project directory |

## Architecture

```
index.html (203 KB source)
    │
    ├─ gzip -9 ──────────────────────────► index.html.gz (46 KB)
    │                                              │
    │                              Upload via /upload endpoint
    │                              (no recompilation needed)
    │                                              │
    └─ ycint.ino (210 KB source) ──► compiled firmware (~520 KB flash, 51%)
                                          │
                                    arduino-cli upload
                                          │
                                    ESP8266 (192.168.88.154)
                                          │
                              ┌───────────┴───────────┐
                              │  Serve from SPIFFS if  │
                              │  index.html.gz exists  │
                              │  else fallback to      │
                              │  PROGMEM (index_gz.h)  │
                              └────────────────────────┘
```

The HTML is gzip-compressed and stored in ESP SPIFFS (filesystem). The ESP serves it with `Content-Encoding: gzip`, so the browser decompresses it automatically. A PROGMEM copy is kept as fallback for first boot or if SPIFFS is empty.

## Prerequisites

- **Arduino CLI** installed (Homebrew: `brew install arduino-cli`)
- **ESP8266 core** installed: `arduino-cli core install esp8266:esp8266`
- **Adafruit libraries** installed:
  ```bash
  arduino-cli lib install "Adafruit SSD1306"
  arduino-cli lib install "Adafruit ADS1X15"
  arduino-cli lib install "Adafruit BusIO"
  arduino-cli lib install "Adafruit GFX Library"
  ```
- **ESP8266** connected via USB (serial port: `/dev/cu.usbserial-0001`)

## Workflow

### Updating HTML Only (No Reflash Needed)

Edit `index.html`, then upload the compressed file to the running ESP:

```bash
cd ~/projects/azelctrl/satellite-tracker

# Compress
gzip -9 -c index.html > index.html.gz

# Upload to ESP (no recompilation or reflashing)
curl -F "html=@index.html.gz" http://192.168.88.154/upload
```

Then refresh the browser page. Changes are immediate.

Alternatively, use the **Upload HTML to ESP** form in the Settings tab of the web interface.

### Updating Firmware Only

```bash
cd ~/projects/azelctrl/ycint

arduino-cli compile \
  --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M \
  .

arduino-cli upload \
  --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M \
  --port /dev/cu.usbserial-0001 \
  .
```

### Full Build (HTML + Firmware)

When both HTML and firmware have changed:

```bash
# 1. Compress HTML and generate fallback header
cd ~/projects/azelctrl/satellite-tracker

gzip -9 -c index.html > index.html.gz

echo "// Auto-generated — do not edit manually" > index_gz.h
echo "// Source: index.html" >> index_gz.h
echo "// Compressed with gzip -9" >> index_gz.h
echo "" >> index_gz.h
echo "const uint8_t index_html_gz[] PROGMEM = {" >> index_gz.h
gzip -9 -c index.html | xxd -i - >> index_gz.h
echo "};" >> index_gz.h
echo "const size_t index_html_gz_len = sizeof(index_html_gz);" >> index_gz.h
cp index_gz.h ~/projects/azelctrl/ycint/

# 2. Compile and flash firmware
cd ~/projects/azelctrl/ycint

arduino-cli compile \
  --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M \
  .

arduino-cli upload \
  --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M \
  --port /dev/cu.usbserial-0001 \
  .

# 3. Upload HTML to SPIFFS
curl -F "html=@/Users/keithl/projects/azelctrl/satellite-tracker/index.html.gz" \
  http://192.168.88.154/upload
```

### One-Liner (Full Build)

```bash
cd ~/projects/azelctrl/satellite-tracker && \
gzip -9 -c index.html > index.html.gz && \
echo "// Auto-generated" > index_gz.h && \
echo "// Source: index.html" >> index_gz.h && \
echo "// Compressed with gzip -9" >> index_gz.h && \
echo "" >> index_gz.h && \
echo "const uint8_t index_html_gz[] PROGMEM = {" >> index_gz.h && \
gzip -9 -c index.html | xxd -i - >> index_gz.h && \
echo "};" >> index_gz.h && \
echo "const size_t index_html_gz_len = sizeof(index_html_gz);" >> index_gz.h && \
cp index_gz.h ~/projects/azelctrl/ycint/ && \
cd ~/projects/azelctrl/ycint && \
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M . && \
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M --port /dev/cu.usbserial-0001 . && \
curl -F "html=@/Users/keithl/projects/azelctrl/satellite-tracker/index.html.gz" http://192.168.88.154/upload
```

## Flash Layout (4M3M Configuration)

| Segment | Size | Contents |
|---------|------|----------|
| **Sketch (irom0_0_seg)** | ~1019 KB | Code + PROGMEM data (hard linker limit) |
| **SPIFFS** | ~3048 KB | Filesystem — HTML, future assets |

Our firmware uses ~520 KB of the 1019 KB sketch segment. The compressed HTML (46 KB) lives in SPIFFS, not PROGMEM, so the firmware can grow without HTML consuming flash space.

## Upload Format

The `/upload` endpoint accepts a **gzip-compressed HTML file** (`.html.gz`). The file format is:

- **Type**: Binary gzip stream (`gzip -9 -c index.html`)
- **Field name**: `html` (multipart form-data)
- **Content-Type**: `application/octet-stream` (the ESP stores it as-is)
- **Response**: `200 OK` with body `Upload complete`

The ESP stores the file as `/index.html.gz` in SPIFFS and serves it with `Content-Encoding: gzip`. The browser decompresses automatically.

## Verification

After flashing, open the ESP in a browser:

```
http://192.168.88.154/
```

The page should load the satellite tracker UI. The connection status should show "Connected" (green dot).

Check the serial console to see which source the ESP is using:
- `SPIFFS: OK ... /index.html.gz (46205B)` — serving from SPIFFS
- `SPIFFS: NOT MOUNTED (using PROGMEM fallback)` — serving from PROGMEM

## Notes

- **HTML changes only**: Compress + curl upload (no reflash)
- **Firmware changes only**: Compile + upload
- **Both changed**: Full build (compile, flash, upload HTML)
- The `index_gz.h` file is **auto-generated** — never edit it manually
- If the ESP serial port differs, check with: `ls /dev/cu.usbserial*`
- The ESP uses a 4 MB flash chip. The `eesz=4M3M` config splits it into ~1 MB sketch segment (code + PROGMEM) and ~3 MB SPIFFS (filesystem). Our firmware uses ~520 KB of the 1 MB sketch segment.
