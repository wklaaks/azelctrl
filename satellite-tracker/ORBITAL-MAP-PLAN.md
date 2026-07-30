# Orbital Map Feature — Plan & Requirements

## Overview

Add a world map beneath the polar plot on the Tracking tab showing the selected satellite's ground track (orbital path projected onto Earth's surface), current position, and ground station location. The map provides spatial context for satellite passes — where the satellite is relative to your station, which direction it's heading, and where it will pass overhead.

## Why This Matters for Amateur Radio Operators

When chasing satellites, the polar plot tells you **where to point your antenna** (az/el). The orbital map tells you **what's happening** — the bigger picture:

- **Is the satellite approaching or departing?** See the ground track relative to your station.
- **Which way is the satellite heading?** Visualize the orbital plane and trajectory.
- **How far away is the satellite?** See the great-circle distance on the map.
- **Is the satellite in sunlight or darkness?** Day/night terminator shows if the satellite has solar power.
- **What's the coverage footprint?** See the area the satellite can "see" from its altitude.
- **Multi-satellite awareness** (future): Compare orbital tracks of multiple satellites to plan QSOs.

## Architecture — ESP-Constrained, Offline-First Design

### The Constraints

1. **Storage**: HTML served from **ESP SPIFFS** (~3 MB filesystem). Current `index.html.gz` is ~46 KB. Ample headroom for embedded map data.
2. **Offline-first**: AZELCtrl must remain fully usable when opened from the ESP with **no internet access**. This is the primary use case — the user is at their radio station, connected to the ESP via WiFi/AP, with no internet.
3. **Current offline pattern**: All existing functionality works offline. TLEs and SatNOGS data are cached in `localStorage` after an initial online fetch. The polar plot is drawn with Canvas — no external dependencies.

### The Problem

A naive Leaflet + CDN tile approach breaks entirely offline:
- Leaflet JS/CSS from CDN → fails to load → map doesn't render
- Map tiles from tile servers → fails to load → blank map even if Leaflet loads
- User sees a broken page section or JavaScript errors

### The Solution: Canvas-Based Offline Map

**Embed everything in the HTML. No external dependencies. Works 100% offline.**

Instead of Leaflet (which requires CDN + tile servers), build a lightweight Canvas-based world map:

| Resource | Source | Size Impact on SPIFFS |
|----------|--------|----------------------|
| World coastline data | Embedded simplified GeoJSON (1:10m) | ~500 KB |
| Map rendering (Canvas) | Custom JS in index.html | ~8 KB |
| Satellite positions | Computed client-side (satellite.js — already loaded) | 0 bytes |
| Terminator line | Computed client-side (Julian date math) | 0 bytes |
| Grid lines, labels | Computed/rendered client-side | 0 bytes |
| Map container HTML | Added to index.html | ~1 KB |

**Total additional SPIFFS storage: ~510 KB** (17% of 3 MB — leaves 2.5 MB for future features).

**Coastline quality**: With 3 MB SPIFFS (instead of ~300 KB flash headroom), we can use Natural Earth 1:10m resolution coastlines instead of 1:500m. This means recognizable island shapes, peninsulas, bays, and country-level detail — not just blobby continent outlines. The Canvas renderer handles the extra points (2000-5000 per continent vs. 50-100) with no performance impact.

### How It Works

```
┌─────────────────────────────────────────────────┐
│  Browser (client) — NO INTERNET NEEDED          │
│  ┌───────────────────────────────────────────┐   │
│  │  index.html (from ESP SPIFFS, ~730 KB total)│  │
│  │  ┌─────────────────────────────────────┐  │   │
│  │  │  Everything embedded in HTML:       │  │   │
│  │  │  - World coastline (1:10m, ~500 KB) │  │   │
│  │  │  - Canvas map renderer              │  │   │
│  │  │  - satellite.js (already embedded)  │  │   │
│  │  └─────────────────────────────────────┘  │   │
│  │                                            │   │
│  │  Rendering pipeline:                       │   │
│  │  1. Draw ocean background (dark blue)      │   │
│  │  2. Draw land masses (dark green/grey)     │   │
│  │  3. Draw lat/lon grid lines                │   │
│  │  4. Draw day/night terminator overlay       │   │
│  │  5. Draw satellite ground track             │   │
│  │  6. Draw markers (sat, station)             │   │
│  └───────────────────────────────────────────┘   │
└─────────────────────────────────────────────────┘
```

**No network requests after page load.** Everything is computed and rendered from data embedded in the HTML file served from ESP SPIFFS.

### Offline-First Design Principles

1. **Map works offline by default** — no CDN, no tile servers, no external dependencies
2. **World data is embedded** — high-quality coastline GeoJSON (~500 KB, Natural Earth 1:10m) baked into the HTML
3. **Same visual quality offline/online** — no degradation; the map looks the same regardless of connectivity
4. **Optional enhancement online** — if internet IS available, offer to enhance with satellite imagery tiles (purely optional, map works fine without)
5. **Consistent with existing AZELCtrl pattern** — just like TLEs: fetch once, cache forever, works offline

### Why Canvas Instead of Leaflet

| Factor | Leaflet (CDN + tiles) | Canvas (embedded) |
|--------|----------------------|-------------------|
| Works offline | ❌ No — requires CDN + tiles | ✅ Yes — everything embedded |
| SPIFFS storage | ~8KB (but broken offline) | ~510KB (fully functional, high quality) |
| External dependencies | Leaflet JS/CSS, tile servers | None |
| Page load time | Multiple HTTP requests | Single file, instant |
| Visual consistency | Degraded offline (blank) | Same appearance always |
| Zoom/pan | Built-in | Custom implementation needed |
| Tile layer switching | Easy | N/A (single embedded style) |
| Maintenance | CDN availability risk | Self-contained |

**Canvas wins for offline-first.** The trade-off is that we need to implement our own pan/zoom, but for a satellite tracking map that's straightforward. With 3 MB SPIFFS, we can embed high-quality coastline data that makes the Canvas map genuinely useful.

### World Coastline Data

Source: Natural Earth 1:10m simplified coastline (public domain)
- **1:10m resolution**: ~500 KB (island-level detail, recognizable coastlines)
- Further simplified to key outlines only if needed: ~150 KB (still high quality)

With 3 MB SPIFFS storage, we have ample space for high-quality coastline data. The 1:10m resolution gives operators a map that actually looks useful — you can identify countries, see where the satellite is relative to land masses, and recognise familiar geography.

The data is a JSON array of continent/region outlines as `[lat, lon]` coordinate pairs. We embed it as a JavaScript constant and render it on the Canvas.

Example (simplified Africa outline — actual data has all continents):
```javascript
const WORLD_OUTLINES = [
  // Africa (simplified)
  [[37, -9], [35, 0], [30, 10], [25, 32], [12, 43],
   [10, 50], [-11, 41], [-26, 28], [-35, 20], [-35, 18],
   [-28, 16], [-18, 12], [-5, 12], [0, 10], [4, 9],
   [5, -4], [15, -17], [21, -17], [37, -9]],
  // ... other continents
];
```

**Simplification strategy**: Use Douglas-Peucker algorithm to reduce points while maintaining recognizable shapes. Target: 2000-5000 points per continent (vs. 50-100 in the old 1:500m approach).

### Optional Online Enhancement

If the user HAS internet, we can offer an upgrade path:

```
┌─────────────────────────────────────────────────┐
│  ORBITAL MAP  [Offline mode]  [Enhance ▼]       │
├─────────────────────────────────────────────────┤
│  [Canvas map with embedded coastlines]           │
│  (Works perfectly — no internet needed)          │
└─────────────────────────────────────────────────┘
```

Clicking "Enhance" attempts to load satellite imagery tiles from CDN. If successful, the Canvas map is replaced with a Leaflet map for richer imagery. If it fails, the Canvas map remains — no degradation.

This is Phase 3+ only. Phase 1-2 use Canvas exclusively.

## Implementation Details

### 1. Canvas Map Renderer

A lightweight Canvas-based world map renderer (~8KB of JavaScript) that draws:

```javascript
class OrbitalMapRenderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.centerLat = 0;
    this.centerLon = 0;
    this.zoom = 1; // 1 = full world, 2 = 2x zoom, etc.
  }

  project(lat, lon) {
    // Equirectangular projection: lat/lon → canvas x/y
    const x = (lon - this.centerLon + 180) / 360 * this.canvas.width;
    const y = (90 - lat) / 180 * this.canvas.height;
    return [x, y];
  }

  render(groundTrack, satellitePos, stationPos, terminator) {
    // 1. Fill ocean background
    this.ctx.fillStyle = '#0c1929'; // Dark blue
    this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);

    // 2. Draw land masses from embedded coastline data
    for (const outline of WORLD_OUTLINES) {
      this.drawPolygon(outline, '#1a3a2a'); // Dark green
    }

    // 3. Draw day/night terminator overlay
    if (terminator) {
      this.drawTerminator(terminator);
    }

    // 4. Draw ground track
    if (groundTrack) {
      this.drawPolyline(groundTrack.past, '#38bdf8', 2, 1);
      this.drawPolyline(groundTrack.future, '#38bdf8', 2, 0.6, [5, 5]);
    }

    // 5. Draw markers
    if (satellitePos) this.drawMarker(satellitePos, '#38bdf8', 'diamond');
    if (stationPos) this.drawMarker(stationPos, '#22c55e', 'home');

    // 6. Draw grid lines
    this.drawGrid();
  }
}
```

**Projection**: Equirectangular (simple lat/lon → x/y mapping). Good enough for global overview. No distortion issues at this scale.

**Rendering pipeline** (runs every 30 seconds):
1. Clear canvas
2. Draw ocean background (dark blue)
3. Draw land masses from embedded coastline data (dark green/grey)
4. Draw day/night terminator overlay (semi-transparent black)
5. Draw satellite ground track (blue lines)
6. Draw markers (satellite = cyan diamond, station = green home)
7. Draw lat/lon grid lines (subtle)

### 2. Embedded World Coastline Data

Source: Natural Earth 1:10m simplified coastline (public domain)
- ~500 KB embedded as JavaScript constant
- 2000-5000 points per continent for recognizable detail
- Stored as `[lat, lon]` coordinate pairs
- Canvas renders line segments — no performance impact at this scale

**Simplification strategy**: Use Douglas-Peucker algorithm to reduce points while maintaining recognizable shapes. Target: 2000-5000 points per continent.

### 3. Orbital Ground Track Computation

Already have `satellite.js` loaded. Use the same propagation logic as Ground Station:

```javascript
// Generate ground track points
function generateGroundTrack(satrec, durationMinutes = 90, stepMinutes = 2) {
  const now = new Date();
  const points = [];
  
  // Past track (solid line)
  for (let i = durationMinutes; i >= 0; i -= stepMinutes) {
    const time = new Date(now.getTime() - i * 60 * 1000);
    const pos = propagateSatellite(satrec, time);
    if (pos) points.push([pos.lat, pos.lon]);
  }
  
  // Future track (dashed line)
  for (let i = stepMinutes; i <= durationMinutes; i += stepMinutes) {
    const time = new Date(now.getTime() + i * 60 * 1000);
    const pos = propagateSatellite(satrec, time);
    if (pos) points.push([pos.lat, pos.lon]);
  }
  
  return { past: points.slice(0, points.length / 2 + 1), future: points.slice(points.length / 2) };
}
```

**Key decisions**:
- **Step interval**: 2 minutes (balances accuracy vs. performance)
- **Duration**: 90 minutes past + 90 minutes future (one full orbit for LEO)
- **Update frequency**: Every 30 seconds (orbital tracks change slowly)
- **Reuse existing `computeTopo()` logic** for satellite propagation

### 4. Day/Night Terminator Line

Computed client-side using Julian date math (from Ground Station's `terminator-line.jsx`):

```javascript
function computeTerminator() {
  // Julian date, GMST, sun ecliptic position
  // → sun equatorial position → hour angle for each longitude
  // → terminator latitude for each longitude
  // Returns array of [lat, lon] points
}
```

**Visual effect**: Night side of Earth gets a semi-transparent dark overlay. The terminator line marks the boundary. This tells the operator if the satellite is in sunlight (solar panels active) or darkness.

### 5. Map Overlays

| Overlay | Style | Purpose |
|---------|-------|---------|
| Past ground track | Solid line, blue (#38bdf8) | Where the satellite has been |
| Future ground track | Dashed line, blue (#38bdf8, opacity 0.6) | Where the satellite is heading |
| Current satellite position | Diamond marker, cyan (#38bdf8) with glow | Real-time position |
| Ground station location | Home icon, green (#22c55e) | Your location |
| Coverage footprint | Circle, semi-transparent cyan | Area visible from satellite |
| Day/night terminator | Semi-transparent dark overlay | Solar illumination |
| Great-circle path | Dotted line, yellow (#eab308) | Path from station to satellite |

### 6. Map Controls

Compact controls that match AZELCtrl's UI style:

```
┌─────────────────────────────────────────────────┐
│  ORBITAL MAP                    [↺ Reset] [⊕]   │
├─────────────────────────────────────────────────┤
│                                                 │
│  [Canvas map with overlays]                     │
│                                                 │
│  SAT: 42.3°N  18.4°E   ALT: 408km   VEL: 7.6km/s│
│  DIST: 1,234km   NEXT PASS: 12m 34s            │
└─────────────────────────────────────────────────┘
```

**Controls**:
- Reset view button (↺ = center on ground station)
- Center on satellite button (⊕)
- Pan: mouse drag
- Zoom: mouse wheel or +/- buttons

### 7. Integration with Existing AZELCtrl

**Location in page**: Beneath the polar plot on the Tracking tab.

```html
<!-- Existing polar plot -->
<div class="polar-plot-container">...</div>

<!-- NEW: Orbital map -->
<div class="orbital-map-container">
  <div class="map-header">
    <span class="map-title">ORBITAL MAP</span>
    <div class="map-controls">
      <button id="mapReset">↺ Reset</button>
      <button id="mapCenterSat">⊕ Sat</button>
    </div>
  </div>
  <canvas id="orbitalMap" class="orbital-map"></canvas>
  <div class="map-footer">
    <span class="sat-position">...</span>
    <span class="sat-distance">...</span>
  </div>
</div>
```

**Data flow**:
1. User selects satellite → TLE loaded → `satrec` created (existing)
2. Every 30 seconds: propagate TLE → compute ground track → render map
3. Map updates are lightweight (just re-render the canvas)
4. No additional HTTP requests to ESP

**CSS considerations**:
- Map height: 250-300px (compact, doesn't dominate the page)
- Dark theme: Canvas draws in dark colors matching AZELCtrl
- Responsive: Canvas container uses flexbox, adapts to screen size
- Hidden when no satellite selected

### 8. Performance Considerations

| Concern | Solution |
|---------|----------|
| Coastline rendering | ~500 KB data, ~15K total points — Canvas line segments render in <10ms |
| Orbital computation | satellite.js is fast; 90 points × 2 = 180 propagations per update |
| Map updates | Canvas re-render is fast (~16ms at 60fps) |
| Memory | No external libraries; total map code ~510 KB in SPIFFS |
| Battery (mobile) | 30-second update interval is conservative |

### 9. Feature Priorities

**Phase 1 (MVP)**:
- [ ] Generate high-quality coastline data (Natural Earth 1:10m → simplified JSON → embedded JS constant)
- [ ] Canvas map with embedded coastline data
- [ ] Past/future ground track (90 min each)
- [ ] Current satellite position marker
- [ ] Ground station marker
- [ ] Map appears beneath polar plot when satellite is selected
- [ ] Pan (mouse drag) and zoom (mouse wheel)

**Phase 2 (Enhanced)**:
- [ ] Day/night terminator overlay
- [ ] Coverage footprint circle
- [ ] Great-circle path from station to satellite
- [ ] Map footer with satellite info (altitude, velocity, distance)
- [ ] Reset view / center on satellite buttons

**Phase 3 (Advanced)**:
- [ ] Multiple satellite tracks (color-coded)
- [ ] Pass prediction markers on ground track
- [ ] Toggle overlays (past track, future track, terminator, etc.)
- [ ] Map follows satellite during tracking
- [ ] Optional: If internet available, offer Leaflet enhancement with satellite imagery

## Visual Design

### Color Scheme (matching AZELCtrl dark theme)

```css
/* Map container */
.orbital-map-container {
  background: var(--bg);
  border: 1px solid var(--border);
  border-radius: 4px;
  overflow: hidden;
}

/* Map header */
.map-header {
  background: #000;
  color: var(--text);
  padding: 4px 8px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  border-bottom: 1px solid var(--border);
}

/* Map footer */
.map-footer {
  background: var(--bg);
  color: var(--text);
  padding: 4px 8px;
  font-family: monospace;
  font-size: 11px;
  border-top: 1px solid var(--border);
  display: flex;
  gap: 12px;
}

/* Overlay colors */
.past-track { stroke: #38bdf8; }       /* Cyan — matches polar plot */
.future-track { stroke: #38bdf8; stroke-dasharray: 5 5; }
.satellite-marker { fill: #38bdf8; }
.station-marker { fill: #22c55e; }      /* Green */
.terminator-overlay { fill: #000; fill-opacity: 0.4; }
.great-circle { stroke: #eab308; }      /* Yellow */
.coverage-circle { stroke: #38bdf8; fill-opacity: 0.1; }
```

### Map Header Layout

```
┌──────────────────────────────────────────────────────────────┐
│  ORBITAL MAP                       [CARTO Dark ▼] [⊕] [🏠]  │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│                    [Leaflet map]                             │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│  SAT: 42.3°N 18.4°E  ALT: 408km  VEL: 7.6km/s  DIST: 1,234km│
└──────────────────────────────────────────────────────────────┘
```

Controls in header:
- Tile layer dropdown
- Reset view button (⊕ = center on ground station)
- Center on satellite button (🏠 = center on satellite)

## What We're NOT Building

- **No server-side map tiles** — everything embedded, fully offline
- **No 3D globe** — 2D map is simpler and fits the compact UI
- **No real-time tracking of multiple satellites** — Phase 1 is single satellite
- **No online dependency** — works 100% offline from ESP; no CDN, no tile servers
- **No map editing or annotation** — read-only visualization

## References

- Ground Station's satellite map: `~/projects/ground-station/frontend/src/components/satellites/satellite-map.jsx`
- Ground Station's tile layers: `~/projects/ground-station/frontend/src/components/common/tile-layers.jsx`
- Ground Station's terminator line: `~/projects/ground-station/frontend/src/components/common/terminator-line.jsx`
- Ground Station's icons: `~/projects/ground-station/frontend/src/components/common/dataurl-icons.jsx`
- Natural Earth data: https://www.naturalearthdata.com/
- satellite.js (already loaded): https://github.com/shashanksingh/satellite.js

## Next Steps

1. Create the plan document (this file) ✓
2. Generate high-quality coastline data:
   - Download Natural Earth 1:10m coastline data
   - Simplify with Douglas-Peucker algorithm (~500 KB target)
   - Convert to embedded JavaScript `WORLD_OUTLINES` constant
3. Add map container HTML beneath polar plot on Tracking tab
4. Implement Canvas map renderer (equirectangular projection, pan/zoom)
5. Implement ground track computation (reuse satellite.js propagation)
6. Add satellite and ground station markers
7. Implement map controls (reset view, center on sat, pan, zoom)
8. Add day/night terminator overlay (Phase 2)
9. Test with live satellite data
10. Iterate on visual design based on feedback
