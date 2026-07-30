# Triangle Fix Troubleshooting Guide

## Problem Description

Large triangular fill artifacts appear at the left/right vertical edges of the canvas map, especially when panning. Triangles also appear at the bottom of the map over Antarctica, even without panning. The triangles are ocean-colored (`#0c1929`), suggesting the land fill (`#1a2a1a`) is not covering expected areas — `fill()` is creating triangular gaps where the ocean background shows through.

### Key Observations

- Triangles appear at left/right vertical screen edges, partial height
- Triangles move with map pan
- Triangles also appear at bottom of map over Antarctica without panning
- "Sea-coloured area seems to have grown larger in the south where antarctica is"
- Antarctica ring (Ring 436) is 2325 points, lat range [-85.2, -63.2], lon range [-180.0, 179.8]
- First point: lon=-180.0 (projects to x=0, left edge)
- Last point: lon=179.7 (projects to x≈w, right edge)
- The ring is continuous (no wraps between consecutive points)
- `closePath()` connects x≈w back to x=0, creating a line across the map

## Techniques Tried (All Failed)

### 1. closePath() per subpath (commit ba7e8b0)
- Split ring at world-edge wraps, call `closePath()` on each subpath
- **Failed**: `closePath()` draws a line from last to first point, creating triangles

### 2. fill("evenodd") (commit ba7e8b0)
- Use `fill("evenodd")` instead of `fill()`
- **Failed**: evenodd creates unpredictable fill behavior with self-intersecting paths

### 3. Subpaths with individual closePath() (commit ee1bf8a)
- Split into subpaths, fill each individually with `closePath()`
- **Failed**: Same issue — `closePath()` creates cross-map lines

### 4. No closePath(), fill open subpaths (commit ee1bf8a)
- Don't call `closePath()`, let `fill()` treat open paths as closed
- **Failed**: `fill()` still creates the closing line for fill purposes, creating triangles

### 5. lonOffset approach (commit 9f97837)
- Adjust longitude by ±360° to keep consecutive points close
- **Failed**: Doesn't handle the wrap between first and last points of the ring

### 6. Conditional closePath() (commit 2aaf5dd)
- Only call `closePath()` if first and last points are close
- **Failed**: `fill()` treats open paths as closed, creating the same triangle

### 7. Midpoint split (commit 9c3240f)
- Split subpaths at midpoint when first/last points are far apart
- **Failed**: Still creates triangles — the midpoint split doesn't correctly identify the antimeridian crossing

### 8. fill("evenodd") on entire ring (commit 4430d30)
- Draw entire ring as one path, use `fill("evenodd")`
- **Failed**: Created "many horizontal lines moving all over the place"

## Root Cause Analysis

The fundamental issue is that Antarctica's ring (Ring 436) starts at lon=-180 and ends at lon=179.7. These are geographically close (both at the antimeridian), but after equirectangular projection with centerLon=0:
- lon=-180 → x=0 (left edge)
- lon=179.7 → x≈w (right edge)

The ring goes continuously from lon=-180 to lon=179.7, passing through lon=0. There are NO wraps between consecutive points. So the entire ring is one subpath, and the first/last points are on opposite sides of the map.

When `closePath()` is called, it draws a line from x≈w (right edge) back to x=0 (left edge), creating a line across the entire map. The `fill()` then fills the triangle created by this line.

When `closePath()` is NOT called, `fill()` still treats the open path as closed (connecting last to first), creating the same triangle.

## Files Involved

- `/Users/keithl/projects/azelctrl/satellite-tracker/orbital-map.js` — Standalone JS file (833+ lines)
- `/Users/keithl/projects/azelctrl/satellite-tracker/index.html` — Main HTML file with orbital-map.js inlined
- `/Users/keithl/projects/azelctrl/satellite-tracker/world_coastline.js` — Coastline data (560 rings, 44,994 points)

## The `_drawRing()` Method

Located in `orbital-map.js` (lines ~175-228) and inlined in `index.html`.

Current implementation (after all fixes):
```javascript
_drawRing(ring) {
    const ctx = this.ctx;
    if (ring.length < 3) return;

    const w = this._logicalWidth || 800;
    const h = this._logicalHeight || 280;

    ctx.fillStyle = '#1a2a1a';
    ctx.strokeStyle = '#2a4a2a';
    ctx.lineWidth = 0.5;

    // Project all points
    const points = [];
    for (let i = 0; i < ring.length; i++) {
      const [x, y] = this.project(ring[i][0], ring[i][1]);
      points.push([x, y]);
    }

    // Split into subpaths at world-edge wraps
    const subpaths = [];
    let current = [];
    let prevX = 0;
    for (let i = 0; i < points.length; i++) {
      const [x, y] = points[i];
      if (i > 0 && Math.abs(x - prevX) > w * 0.8) {
        if (current.length > 2) subpaths.push(current);
        current = [];
      }
      current.push([x, y]);
      prevX = x;
    }
    if (current.length > 2) subpaths.push(current);

    // For each subpath, check if first and last points are far apart.
    // If so, split at the midpoint to avoid fill() creating triangles
    const finalSubpaths = [];
    for (const subpath of subpaths) {
      const firstX = subpath[0][0];
      const lastX = subpath[subpath.length - 1][0];
      if (Math.abs(firstX - lastX) > w * 0.5) {
        const mid = Math.floor(subpath.length / 2);
        const half1 = subpath.slice(0, mid + 1);
        const half2 = subpath.slice(mid);
        if (half1.length > 2) finalSubpaths.push(half1);
        if (half2.length > 2) finalSubpaths.push(half2);
      } else {
        finalSubpaths.push(subpath);
      }
    }

    // Fill each subpath as a separate closed polygon
    for (const subpath of finalSubpaths) {
      ctx.beginPath();
      ctx.moveTo(subpath[0][0], subpath[0][1]);
      for (let i = 1; i < subpath.length; i++) {
        ctx.lineTo(subpath[i][0], subpath[i][1]);
      }
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
    }
  }
```

## The `project()` Method

Located in `orbital-map.js` (lines ~55-67):

```javascript
project(lat, lon) {
    const w = this._logicalWidth;
    const h = this._logicalHeight;
    let lonShift = lon - this.centerLon;
    while (lonShift < -180) lonShift += 360;
    while (lonShift > 180) lonShift -= 360;
    const x = ((lonShift + 180) / 360) * w;
    const y = ((90 - lat) / 180) * h;
    return [x, y];
  }
```

## Development Workflow

### 1. Edit the source files

Edit BOTH files:
- `orbital-map.js` (standalone file)
- `index.html` (inlined version of orbital-map.js)

The inlined version in `index.html` must match `orbital-map.js` exactly.

### 2. Test locally

```bash
cd /Users/keithl/projects/azelctrl/satellite-tracker
python3 -m http.server 8000
```

Open `http://localhost:8000/index.html` in a browser and check the Map tab.

### 3. Verify files are consistent

```bash
cd /Users/keithl/projects/azelctrl/satellite-tracker
python3 -c "
with open('orbital-map.js', 'r') as f:
    js = f.read()
with open('index.html', 'r') as f:
    html = f.read()
import re
m1 = re.search(r'_drawRing\(ring\) \{.*?ctx\.stroke\(\);\n  \}', js, re.DOTALL)
m2 = re.search(r'_drawRing\(ring\) \{.*?ctx\.stroke\(\);\n  \}', html, re.DOTALL)
if m1 and m2:
    print('Files match:', m1.group() == m2.group())
"
```

### 4. Gzip and upload to ESP

```bash
cd /Users/keithl/projects/azelctrl/satellite-tracker
gzip -c index.html > index.html.gz
curl -s -F "file=@index.html.gz" http://192.168.88.154/upload
```

### 5. Verify deployed code

```bash
curl -s http://192.168.88.154/ | gunzip | grep -c 'closePath'
```

### 6. Commit changes

```bash
cd /Users/keithl/projects/azelctrl
git add -A
git commit -m "Fix: [describe the fix]"
```

### 7. Hard reload the ESP page

In the browser, do a hard reload (Cmd+Shift+R on macOS) of `http://192.168.88.154/` to ensure the new code is loaded.

## Resolved: Canvas-Edge Close (Approach G)

The problem was implemented and committed on [date]. The fix replaces `closePath()` with canvas-edge-aware closing paths:

### What changed in `_drawRing()`

Instead of always calling `closePath()` (which draws a straight line from the last point to the first point, cutting across the map), the code now determines **how** to close each subpath:

| Condition | Close Mode | Behavior |
|-----------|-----------|----------|
| Subpath touches left edge (`minX < w * 0.02`) | `left` | Go to `x=0`, then along left edge |
| Subpath touches right edge (`maxX > w * 0.98`) | `right` | Go to `x=w`, then along right edge |
| Subpath spans full width (`span > w * 0.8`) | `top` or `bottom` | Go to `y=0` or `y=h`, then along edge |
| First/last gap > 0.5w (inland, not edge) | `direct` (midpoint split) | Same as before — split at midpoint |
| Normal subpath | `direct` | `closePath()` as before |

### Effective fixes

- **Panning fix**: When a landmass (Alaska, etc.) crosses the left/right edge during panning, each split subpath now closes to the nearest canvas edge instead of drawing a diagonal line across the map. The fill extends cleanly to the edge.

- **Antarctica fix**: The full-width ring spanning [0, w] is detected as `isFullWidth` and closed via the bottom edge (since avgY > h/2), filling Antarctica correctly.

- **Arctic fix**: Same approach — closed via the top edge.

### Files modified

- `orbital-map.js` — `_drawRing()` method
- `index.html` — inlined `_drawRing()` method

Both files must be kept in sync. Verify with `python3 -c "..."` as shown above.

### Approach A: Fix the coastline data
Instead of fixing the rendering, fix the data. The issue is that Antarctica's ring starts at lon=-180 and ends at lon=179.7. If we modify the ring to start at lon=0 (or any longitude that's not at the antimeridian), the first and last points would be close together after projection, and `closePath()` would work correctly.

Steps:
1. Modify `generate_coastline.py` to rotate rings so they don't start/end at the antimeridian
2. Regenerate `world_coastline.js`
3. Upload to SPIFFS on the ESP

### Approach B: Use a different projection
Instead of equirectangular, use a projection that handles Antarctica better (e.g., polar stereographic for southern hemisphere).

Steps:
1. Modify `project()` in `orbital-map.js`
2. Update all rendering code that depends on projection

### Approach C: Pre-process rings to remove antimeridian crossings
Before rendering, split rings at the antimeridian into separate rings that don't cross it.

Steps:
1. Add a `preprocessRings()` method to `OrbitalMapRenderer`
2. For each ring, detect if it crosses the antimeridian
3. Split the ring into two separate rings at the crossing point
4. Store the split rings in `this.rings`

### Approach D: Use clipping regions
Instead of trying to fix the fill, use canvas clipping regions to prevent triangles from being drawn outside the expected area.

Steps:
1. Define a clipping region that matches the map bounds
2. Apply the clipping region before drawing land
3. Remove the clipping region after drawing land

### Approach E: Draw land as individual pixels
Instead of using `fill()`, draw each land pixel individually. This avoids the path-filling issue entirely.

Steps:
1. Create a pixel buffer for the land mask
2. For each ring, set pixels to land color
3. Draw the pixel buffer to the canvas

### Approach F: Use SVG instead of Canvas
SVG handles path filling correctly, including self-intersecting paths and antimeridian crossings.

Steps:
1. Convert the Canvas-based map to SVG
2. Use SVG path elements for coastlines
3. Let the browser handle the rendering
