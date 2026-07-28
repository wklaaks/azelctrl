# AZELCtrl — Build & Flash Guide

This document explains how to build the satellite tracker firmware and flash it to the ESP8266.

## File Locations

| File | Path | Description |
|------|------|-------------|
| **HTML source** | `~/projects/azelctrl/satellite-tracker/index.html` | Satellite tracker UI (203 KB) — edit this file for UI changes |
| **Firmware source** | `~/projects/azelctrl/ycint/ycint.ino` | ESP8266 Arduino sketch — edit for firmware changes |
| **Compressed HTML** | `~/projects/azelctrl/ycint/index_gz.h` | Auto-generated C header — **do not edit manually** |
| **ESP firmware** | `~/projects/azelctrl/ycint/` | Arduino project directory |

## Architecture

```
index.html (203 KB source)
    │
    ├─ gzip -9 ──────────────────► index_gz.h (45 KB compressed C array)
    │                                      │
    │                              Included in ycint.ino
    │                                      │
    └─ ycint.ino (192 KB source) ──► compiled firmware (~486 KB flash, 46%)
                                          │
                                    arduino-cli upload
                                          │
                                    ESP8266 (192.168.88.154)
```

The HTML is gzip-compressed and embedded as a byte array in the firmware's PROGMEM. The ESP serves it with `Content-Encoding: gzip`, so the browser decompresses it automatically. This reduces the embedded size from 203 KB to 45 KB.

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

## Build Process

### Step 1: Compress HTML → C Header

Generate the gzipped HTML header from `index.html`:

```bash
cd ~/projects/azelctrl/satellite-tracker

echo "// Auto-generated — do not edit manually" > index_gz.h
echo "// Source: index.html" >> index_gz.h
echo "// Compressed with gzip -9" >> index_gz.h
echo "" >> index_gz.h
echo "const uint8_t index_html_gz[] PROGMEM = {" >> index_gz.h
gzip -9 -c index.html | xxd -i - >> index_gz.h
echo "};" >> index_gz.h
echo "const size_t index_html_gz_len = sizeof(index_html_gz);" >> index_gz.h

# Copy to firmware directory
cp index_gz.h ~/projects/azelctrl/ycint/index_gz.h
```

### Step 2: Compile Firmware

```bash
cd ~/projects/azelctrl/ycint

arduino-cli compile \
  --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M \
  .
```

**Expected output:**
- Code in flash: ~486 KB / ~1024 KB (irom0_0_seg linker segment)
- RAM: ~38 KB / 80 KB (48%)
- IRAM: ~61 KB / 65 KB (93%)

**Flash layout (4M3M configuration):**
- **Sketch (irom0_0_seg)**: ~1019 KB — code + PROGMEM data (hard linker limit)
- **SPIFFS**: ~3048 KB — filesystem partition (not used for code)

Our firmware uses ~486 KB of the 1019 KB sketch segment, leaving **~533 KB** for additional PROGMEM data. The compressed HTML is 45 KB, so we have ample headroom.

**If the HTML grows beyond ~533 KB compressed**, we'd hit the linker limit. At that point, we'd need to:
1. Store the HTML in SPIFFS and serve it from the filesystem, or
2. Use more aggressive compression (e.g., custom LZ-based compression)

### Step 3: Upload to ESP

```bash
cd ~/projects/azelctrl/ycint

arduino-cli upload \
  --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M \
  --port /dev/cu.usbserial-0001 \
  .
```

### One-Liner (All Steps)

```bash
cd ~/projects/azelctrl/satellite-tracker && \
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
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2:eesz=4M3M --port /dev/cu.usbserial-0001 .
```

## Verification

After flashing, open the ESP in a browser:

```
http://192.168.88.154/
```

The page should load the satellite tracker UI. The connection status should show "Connected" (green dot).

## Notes

- **HTML changes only**: Run Step 1 → Step 2 → Step 3
- **Firmware changes only**: Run Step 2 → Step 3
- **Both changed**: Run all three steps
- The `index_gz.h` file is **auto-generated** — never edit it manually
- If the ESP serial port differs, check with: `ls /dev/cu.usbserial*`
- The ESP uses a 4 MB flash chip. The `eesz=4M3M` config splits it into ~1 MB sketch segment (code + PROGMEM) and ~3 MB SPIFFS (filesystem). Our firmware uses ~486 KB of the 1 MB sketch segment.
