# Orbital Map Integration — Attempt Summary

## Goal

Add a dedicated "Map" tab to the AZELCtrl web interface showing an offline, Canvas-based world map with:
- Coastline outlines (Natural Earth 1:10m, simplified via Douglas-Peucker)
- Satellite ground tracks (past in solid red, future in dashed red)
- Day/night terminator shading (darkened night region)
- Pan/zoom, world wrapping, grid overlay
- Zero CDN dependencies — all assets embedded or served locally from ESP8266 SPIFFS

## Architecture

### Intended Data Location

The key design decision: **map data must NOT be compiled into the firmware.** The ESP8266 has a ~1 MB flash limit for the sketch (`irom0_0_seg`), and the coastline data alone (~609 KB raw, ~132 KB gzipped) would consume a large portion of that budget. Instead:

- **`index.html`** (~56 KB gzipped) stays small enough to embed as a PROGMEM fallback in the firmware header (`ycint/index_gz.h`). This is the "safety net" — if SPIFFS is empty or corrupted, the device still serves a working (but map-less) interface.
- **`world_coastline.js`** (~132 KB gzipped) lives **only on SPIFFS**. It is never compiled into the firmware. The browser fetches it asynchronously via `fetch('/world_coastline.js')` when the Map tab is opened. This keeps the firmware sketch size low and allows map data updates without reflashing.
- **`orbital-map.js`** (~27 KB) is the renderer code. It was intended to be inlined into `index.html` (so it's available from both PROGMEM and SPIFFS), but the merge process kept corrupting the HTML.

| Component | Location | Size | Compiled into firmware? |
|-----------|----------|------|------------------------|
| `index.html` | PROGMEM fallback + SPIFFS | ~56 KB gzipped | Yes (as `index_gz.h`) |
| `world_coastline.js` | SPIFFS only | ~132 KB gzipped | **No** |
| `orbital-map.js` | Inline in `index.html` (intended) | ~27 KB | Only via `index.html` |
| `OrbitalMapRenderer` class | Canvas-based, equirectangular projection | — | — |

### Why This Separation Matters

The firmware compile limit is ~1019 KB for `irom0_0_seg`. The current firmware (without map) uses ~475 KB. Embedding `index.html.gz` (~56 KB) brings it to ~531 KB — acceptable. But embedding the coastline data (~609 KB raw) would push it to ~1140 KB, exceeding the limit. By keeping coastline data on SPIFFS (~3 MB available), we have plenty of room and can update the map data independently of the firmware.

## What Was Accomplished

1. **Coastline data generation** — Downloaded TopoJSON from `topojson/world-atlas`, applied Douglas-Peucker simplification at 0.05° tolerance, produced `world_coastline.js` with 560 rings, 44,994 points (~132 KB gzipped).

2. **Canvas renderer** — Implemented `OrbitalMapRenderer` with:
   - Equirectangular projection with proper longitude wrapping
   - Device pixel ratio (DPR) support
   - ResizeObserver for tab visibility
   - Graticule grid overlay
   - Ground track rendering with world-edge path breaking (>80% width jump detection)
   - Coastline stroke-only rendering (no fill to avoid artifacts)

3. **Firmware integration** — Updated `ycint.ino` to serve `world_coastline.js.gz` from SPIFFS with `Content-Encoding: gzip`.

4. **Map tab** — Added dedicated "Map" tab with Day/Night and Grid toggle checkboxes.

## Problems Encountered

### 1. HTML Structure Corruption (Critical)

The original `index.html` has an unusual structure: the `<body>` tag sits inside a `<script>` block that starts at position ~485 and ends at ~223305. This is a pre-existing quirk, not something we introduced.

**What happened:** Our Python merge script repeatedly failed to correctly inject `orbital-map.js` and the map tab HTML into this structure. Each attempt produced one of these failures:

- **Nested `<script>` tags:** The merge script inserted a new `<script>` block inside the existing one, causing the browser to throw `SyntaxError: Unexpected token '<'`.
- **Missing map container:** The merge script failed to add `#tab-map` and `#orbitalMapContainer` to the DOM, so the map tab appeared but the canvas never initialized.
- **Duplicate variable declarations:** The merge script appended orbital-map.js code that re-declared variables already in scope (`origOnChange`, `trackSatSelect`, etc.), causing `SyntaxError: Can't create duplicate variable`.
- **Empty page:** The ESP served gzipped HTML that decompressed to malformed content, rendering a blank page.

**Root cause:** The merge script's string search/replace logic couldn't reliably find the correct insertion point in the existing HTML structure. The script used `html.find()` and regex to locate boundaries, but the unusual nesting of `<body>` inside `<script>` made these searches return unexpected positions.

### 2. Day/Night Terminator Shading (Unresolved)

The terminator (day/night boundary) was computed correctly using right ascension-based formulas with GMST and solar declination. The `computeTerminator()` function returns an array of `{ lat, lonSunset, lonSunrise }` points plus `sunLonDeg`.

**What happened:** Multiple approaches to shading the night region all failed:

1. **Strip-fill approach (first attempt):** For each latitude strip, fill between sunset and sunrise x-coordinates. Failed because:
   - Strip height calculation was wrong — strips were only 4px tall with gaps between them, leaving most of the map unshaded.
   - The fill direction was inverted in some hemispheres — night appeared between the terminator and the equator instead of poleward of the terminator.

2. **Polygon approach:** Build a single polygon following the sunset line down, crossing the bottom edge, following the sunrise line up, crossing the top edge. Failed because:
   - World-wrap breaks in the terminator lines created disconnected sub-paths that couldn't be filled as a single polygon.
   - The canvas `fill()` rule (non-zero winding) produced incorrect results when the polygon self-intersected at the map edges.

3. **Inverted fill:** Fill the entire canvas dark, then cut out the day region using `destination-out` composite mode. Failed because:
   - The composite mode erased underlying map layers (coastlines, ground tracks) along with the day region.
   - Couldn't restore the map after cutting out the day region.

4. **Corrected strip-fill (latest attempt):** Fixed strip height to extend to midpoint between latitude steps, and compared projected x-coordinates (not raw longitudes) to determine canvas wrapping. This approach was more correct but still showed issues:
   - Night shading appeared only in certain regions, not consistently across all latitudes.
   - The fill direction logic (`sx < rx` vs `sx > rx`) didn't account for the fact that the night region can span more than 180° of longitude, causing the fill to invert.

**Root cause:** The terminator shading problem is fundamentally a topology problem. The night region is not always a simple "between two lines" shape — it can wrap around the map, span the poles, or cover more than half the map. A simple strip-fill or polygon approach can't handle all these cases correctly.

### 3. Sunset/Sunrise Formula Swap

During debugging, we discovered that `lonSunset` and `lonSunrise` formulas were swapped in `computeTerminator()`. The correct formulas are:
```
lonSunset = sunRADeg + haDeg - GMSTdeg
lonSunrise = sunRADeg - haDeg - GMSTdeg
```

This was fixed, but the shading still didn't work correctly due to the topology issues above.

### 4. Map Tab Integration

The map tab was added to the tabs section and the map container was added before `</body>`, but the `initOrbitalMap()` function was never called because the orbital-map.js code wasn't properly injected into the HTML. The map tab appeared in the UI but showed nothing when clicked.

## Current State

- `index.html` — Reverted to working git version (no map tab, no orbital-map.js).
- `orbital-map.js` — Exists as a separate file with the latest strip-fill terminator logic.
- `world_coastline.js` — Generated and working, served from SPIFFS.
- Firmware — Updated to serve coastline data from SPIFFS.

## Next Steps (If Attempted Again)

1. **Fix HTML merge:** Use a more robust approach to inject orbital-map.js into index.html — e.g., append the `<script>` block after `</body>` instead of trying to insert it inside the existing script structure.

2. **Fix terminator shading:** Consider a different approach:
   - Pre-compute the night region as a set of latitude-longitude polygons that don't wrap around the map.
   - Use a simpler fill rule: for each pixel, check if its longitude is between sunset and sunrise (accounting for wrap-around).
   - Or use a canvas mask: draw the terminator lines, then flood-fill the night region from a known point (e.g., the antipode of the sub-solar point).

3. **Test locally first:** Before uploading to the ESP, test the merged HTML locally with `python3 -m http.server` to catch syntax errors before they reach the device.

## Files

| File | Status |
|------|--------|
| `satellite-tracker/index.html` | Reverted to git version (working, no map) |
| `satellite-tracker/orbital-map.js` | Latest version with strip-fill terminator |
| `satellite-tracker/world_coastline.js` | Generated, working |
| `satellite-tracker/generate_coastline.py` | Working |
| `ycint/ycint.ino` | Updated to serve coastline from SPIFFS |
| `ycint/index_gz.h` | Updated with gzipped HTML fallback |

## OrbitalMapRenderer API

The `orbital-map.js` file exports one class and two functions.

### `OrbitalMapRenderer` class

```javascript
// Create from a <canvas> element
const map = new OrbitalMapRenderer(canvas);

// Resize canvas to match container (called automatically via ResizeObserver)
map.resize();

// Projection: lat/lon → canvas x/y
const [x, y] = map.project(lat, lon);

// Inverse projection: canvas x/y → lat/lon
const [lat, lon] = map.unproject(x, y);

// Set coastline data (from world_coastline.js)
map.setCoastline(WORLD_OUTLINES);

// Set ground track
map.setGroundTrack(pastPoints, futurePoints);  // each: [[lat, lon], ...]

// Set positions
map.setSatPos(lat, lon);       // Satellite position
map.setStationPos(lat, lon);   // Ground station position

// Set day/night terminator
map.setTerminator(result, show);  // result from computeTerminator(), show: boolean

// Center map on a position
map.centerOn(lat, lon);

// Toggle visibility
map.setShowTerminator(true/false);
map.setShowGrid(true/false);

// Render the full map
map.render();
```

### `computeTerminator(date)` function

```javascript
const result = computeTerminator(new Date());
// Returns: { points: [{ lat, lonSunset, lonSunrise }, ...], sunLonDeg }
```

### `initOrbitalMap()` function

```javascript
initOrbitalMap();
// Creates the OrbitalMapRenderer instance, wires up ResizeObserver,
// fetches world_coastline.js, and exposes `orbitalMap` as a global.
```

### Coordinate System

- **Projection:** Equirectangular (lat/lon maps linearly to x/y)
- **X-axis:** Longitude, wrapped relative to `centerLon` to [-180, +180]
- **Y-axis:** Latitude, 90° → top, -90° → bottom
- **Canvas scaling:** Width scales with container; height scales with container (was fixed at 280px, now dynamic)
- **World wrapping:** Points outside the visible range reappear on the opposite edge
- **DPR-aware:** Canvas pixel buffer is scaled by `devicePixelRatio`; drawing uses logical coordinates
