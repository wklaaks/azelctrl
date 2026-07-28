# ESP8266 HTML Serving — Plan

## Current Situation

| Component | Size |
|-----------|------|
| `ycint.ino` (firmware code) | ~196KB source |
| Old embedded HTML (`html_rotator`) | ~4KB |
| New satellite tracker HTML | **~203KB** |
| **Total firmware + new HTML** | **~400KB+ compiled** |

### ESP8266 Flash Constraints

The ESP8266 (Wemos D1 Mini / NodeMCU) typically has:
- **4MB flash** (most common)
- Arduino core + libraries consume ~256-384KB compiled
- PROGMEM data goes into flash (not RAM)
- **Available for our HTML: ~1-2MB** (generous estimate)

### The Problem

The new HTML (203KB) is **50x larger** than the old embedded HTML (4KB). We have two options:

1. **Compress the HTML** and embed the compressed version in PROGMEM
2. **Split the HTML** — keep the core in PROGMEM, load extras from CDN

## Option 1: Compress + Embed (Recommended)

### Approach

Compress the HTML with gzip before embedding in PROGMEM. The ESP8266 can decompress on-the-fly when serving.

**Compression ratio**: HTML compresses very well — typically **80-90% reduction**:
- 203KB uncompressed → ~25-40KB gzip compressed
- This is a **5-8x reduction** — easily fits in flash

### Implementation

```cpp
#include <ESP8266WebServer.h>
#include <Arduino.h>

// Compressed HTML (gzip) — stored as byte array in PROGMEM
const uint8_t html_gz[] PROGMEM = {
  0x1f, 0x8b, 0x08, 0x00, ... // gzip header + compressed data
};
const size_t html_gz_size = sizeof(html_gz);

void serveIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Content-Type", "text/html");
  server.send(200, "text/html", "");
  server.sendContent_P(html_gz, html_gz_size);
}
```

### Pros
- **Everything offline** — no CDN, no internet needed
- **Fits in flash** — 25-40KB vs 203KB
- **Faster serving** — less data to transmit over WiFi
- **Browser decompresses automatically** — all modern browsers handle gzip

### Cons
- Need to gzip the HTML during build (one-time step)
- Cannot easily patch individual parts — must re-gzip and re-flash

### Build Process

```bash
# Compress HTML to gzip
gzip -9 -c index.html > index.html.gz

# Convert to C byte array
xxd -i index.html.gz > index_gz.h

# Include in firmware
// #include "index_gz.h" in ycint.ino
```

## Option 2: Split HTML — Core + CDN

### Approach

Split the HTML into:
1. **Core** (~30KB): HTML structure, CSS, essential JS — embedded in PROGMEM
2. **Libraries** (~170KB): satellite.js, large CSS — loaded from CDN

### Pros
- Smaller PROGMEM footprint
- CDN caching benefits

### Cons
- **Requires internet for initial load** — breaks offline-first design
- CDN availability risk
- Multiple HTTP requests slow down page load
- Not acceptable for offline-first requirement

**❌ Rejected — violates offline-first principle**

## Option 3: Minify + Compress (Best of Both)

### Approach

1. **Minify** the HTML (remove whitespace, comments, shorten variable names)
2. **Compress** with gzip
3. **Embed** the compressed version in PROGMEM

**Expected size**:
- 203KB → minify → ~150KB → gzip → ~20-30KB

### Implementation

```bash
# Minify HTML
html-minifier --collapse-whitespace --remove-comments --remove-empty-attributes index.html > index.min.html

# Compress
gzip -9 -c index.min.html > index.min.html.gz

# Convert to C array
xxd -i index.min.html.gz > index_gz.h
```

## Recommendation: Option 3 (Minify + Compress)

### Steps

1. **Minify the HTML** — reduce size by removing unnecessary whitespace
2. **Compress with gzip** — achieve 80-90% compression
3. **Convert to C byte array** — embed in firmware
4. **Update firmware** — serve gzip-compressed HTML with proper headers
5. **Verify** — test that the page loads correctly from the ESP

### Expected Result

| Metric | Before | After |
|--------|--------|-------|
| HTML source size | 203KB | 203KB (unchanged — we keep the source) |
| Embedded size | 4KB (old HTML) | ~30KB (minified + gzipped) |
| Firmware flash usage | ~384KB | ~414KB |
| Page load time | Fast (small) | Fast (compressed) |
| Works offline | ✅ | ✅ |
| CDN dependency | ❌ | ❌ |

### Build Script

Create a build script that automates the process:

```bash
#!/bin/bash
# build_html.sh — minify, compress, and generate C header

INPUT="index.html"
MINIFIED="index.min.html"
COMPRESSED="index.min.html.gz"
HEADER="index_gz.h"

# Minify (remove comments, collapse whitespace)
# Use python for portability
python3 minify_html.py "$INPUT" > "$MINIFIED"

# Compress
gzip -9 -c "$MINIFIED" > "$COMPRESSED"

# Convert to C array
echo "// Auto-generated — do not edit" > "$HEADER"
echo "// Source: $INPUT" >> "$HEADER"
echo "// Compressed size: $(wc -c < "$COMPRESSED") bytes" >> "$HEADER"
echo "const uint8_t index_html_gz[] PROGMEM = {" >> "$HEADER"
xxd -i "$COMPRESSED" >> "$HEADER"
echo "};" >> "$HEADER"
echo "const size_t index_html_gz_size = sizeof(index_html_gz);" >> "$HEADER"

echo "Generated $HEADER ($(wc -c < "$HEADER") bytes)"
```

### Firmware Changes

```cpp
// In ycint.ino:
#include "index_gz.h"  // Auto-generated header

void setupWebServer() {
  server.on("/", []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Content-Type", "text/html; charset=utf-8");
    server.send(200, "text/html", "");
    server.sendContent_P(index_html_gz, index_html_gz_size);
  });
  
  server.onNotFound([]() {
    // Same as root
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Content-Type", "text/html; charset=utf-8");
    server.send(200, "text/html", "");
    server.sendContent_P(index_html_gz, index_html_gz_size);
  });
  
  // ... rest of API endpoints unchanged ...
}
```

## Alternative: No Compression (if gzip causes issues)

If gzip decompression causes problems with the ESP8266 WebServer, we can embed the minified HTML directly:

```cpp
const char html_tracker[] PROGMEM = R"RAWHTML(
<!DOCTYPE html><html>...minified content...
)RAWHTML";

void serveIndex() {
  server.send(200, "text/html", html_tracker);
}
```

**Minified size**: ~150KB (still fits in 4MB flash)
**Pros**: Simpler, no decompression overhead
**Cons**: Larger PROGMEM, slower WiFi transfer

## Decision Matrix

| Approach | Embedded Size | Offline | Complexity | Recommended |
|----------|--------------|---------|------------|-------------|
| Raw embed | 203KB | ✅ | Low | ❌ Too large |
| Minified embed | ~150KB | ✅ | Low | ✅ Fallback |
| Gzip embed | ~30KB | ✅ | Medium | ✅ **Best** |
| CDN split | ~30KB | ❌ | High | ❌ Not offline |

## Next Steps

1. ✅ Create a minification script for the HTML
2. ✅ Test gzip compression ratio
3. ✅ Update firmware to serve compressed HTML
4. Verify the page loads correctly from the ESP
5. Test offline functionality
