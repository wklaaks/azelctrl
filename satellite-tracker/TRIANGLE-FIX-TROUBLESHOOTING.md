# Triangle Fix — RESOLVED

**Status: fixed** (longitude unwrapping). This document is kept as a record of
the root cause and of the 12 approaches that failed, so the dead ends aren't
re-explored.

## Problem (historical)

Large triangular fill artifacts appeared at the left/right vertical edges of the
canvas map, especially when panning. Triangles also appeared at the bottom of the
map over Antarctica without panning. The triangles were ocean-coloured
(`#0c1929`) where land (`#1a2a1a`) was expected, or land-coloured where ocean was
expected — `fill()` was creating triangular gaps and wedges.

## Root cause

Every failed approach shared one assumption: **split each ring wherever its
projected x jumps across the map seam, then decide how to close each fragment.**

That framing is unfixable. Once a ring is split at the seam, the fragments are
*not closed polygons* — they are open polylines whose two endpoints sit on
opposite sides of the canvas. `fill()` must invent a closing edge, and **no
closing rule can be correct for all fragments**, because the correct choice
depends on the ring's winding, the fragment's direction, which edge it touches,
and the local coastline geometry. That is precisely why every heuristic traded
one artifact for another:

- close via the canvas edge → preserves landmass, but fills ocean with land colour
- close via `closePath()` → avoids ocean-fill, but cuts a diagonal through the
  continent and loses large areas of landmass

The measured symptom of this instability: **land area rendered varied by 2.35%
depending on `centerLon`**. Panning literally changed how much land existed.

Antarctica was a second, independent problem. Measured over the data: it is the
**only ring of 560** whose longitudes wind a full turn (exactly 360.0°). Such a
ring encircles a pole and has *no* real last→first edge, so any `closePath()`
draws a line straight across the map.

## The fix: longitude unwrapping

**Never split.** Walk each ring in continuous *unwrapped* longitude —
accumulate the shortest step between consecutive points, so x never jumps the
seam:

```javascript
let d = ring[i][1] - ring[i - 1][1];
while (d < -180) d += 360;
while (d > 180) d -= 360;
u += d;
```

The result is one unbroken polygon, which may extend outside `[0, w]`. Draw it up
to three times, offset by `-w / 0 / +w`, so whatever straddles the seam reappears
on the opposite side. Copies entirely off-canvas are culled.

Each ring is therefore always filled as the single closed polygon it actually is.
No subpaths, no close modes, no winding tests, no 2px inset.

All seam copies go into **one path with one `fill()`** — same-winding subpaths
union cleanly under the nonzero rule, so abutting copies leave no hairline.

**Pole-encircling rings** are the one genuine special case. Detected by total
winding (`Math.abs(winding) > 180`), they are closed *through the pole,
off-canvas* (`polarY = h + 8` south, `-8` north) — never with a line across the
map. The closure spans exactly one full turn so seam copies abut with no gap. The
stroke path skips this closure entirely, so no line is ever drawn over the pole.

`_drawRing()` shrank from 6,466 to 4,113 characters. The identifiers `closeMode`,
`wrapsGlobe`, `isFullWidth`, `touchesLeft`, `touchesRight`, `finalSubpaths` and
`ringArea` are all gone.

## Verification

`_test_ring_fill.js` loads the **actual shipped `_drawRing()`** out of
`orbital-map.js` (or `index.html`), records its fill paths through a fake canvas
context, rasterizes them with nonzero-winding scanlines, and compares against
`_test_reference_mask.js` — an independent, projection-agnostic ground-truth land
mask built by spherical point-in-polygon on the raw lat/lon data.

```bash
node _test_ring_fill.js orbital-map.js          # test current code
node _test_ring_fill.js index.html              # test the inlined copy
node _test_ring_fill.js orbital-map.js old.js   # compare against a baseline
```

Across 10 pan positions:

| | worst error | land-area spread across pans |
|---|---|---|
| old | 1.52% of canvas | 2.35% |
| **new** | **0.11%** | **0.14%** |

The residual 0.11% is sub-pixel disagreement between two independent rasterizers,
and it is now *flat* across all pans — the old code's error varied with
`centerLon`, which is what made panning look broken.

Confirmed in a real browser (steady-state land coverage 31.32–31.40%, 0.08%
spread) and on the live device (31.40–31.47%, 0.07% spread).

A pixel diff against the old renderer showed exactly the reported artifacts: at
`centerLon=-60` a large ocean-fill rectangle top-left plus a **green wedge over
Siberia** at the right edge; at `centerLon=180` lost land along Antarctica's
bottom edge. All absent in the new code.

### Measurement pitfall

The **first** land-coverage measurement after page load reads low (~29.8% vs the
true 31.43%) — a first-paint artifact, not geometry. Always discard one warm-up
`render()` before measuring, and interleave repeats of the same `centerLon`
between other positions to separate ordering effects from position effects.

## Files

- `orbital-map.js` — `_drawRing()` (source of truth)
- `index.html` — inlined copy, must match exactly
- `sync_drawring.py` — syncs the method between the two files
- `_test_ring_fill.js`, `_test_reference_mask.js` — regression test

## Development workflow

### ⚠️ SUPERSEDED (Aug 3, 2026) — single source of truth

The workflow below describes the retired two-file setup (`orbital-map.js` +
`sync_drawring.py`). Those files are DELETED. The renderer is authored ONLY in
`index.html` (inline class). Verify with `node _test_ring_fill.js index.html`.

### 1. Edit `orbital-map.js`, then sync

<details><summary>historical (do not use)</summary>

Edit `_drawRing()` in `orbital-map.js` only, then propagate it:

```bash
cd /Users/keithl/projects/azelctrl/satellite-tracker
python3 sync_drawring.py           # copy into index.html + verify
python3 sync_drawring.py --check   # verify only; non-zero exit if out of sync
```

> The old regex-based sync check in this document silently matched *nothing*
> after the rewrite (it keyed on `ctx.stroke();\n  }`), reporting success while
> comparing empty strings. `sync_drawring.py` brace-matches the method instead
> and fails loudly if either file lacks exactly one `_drawRing`.

### 2. Test

```bash
node --check orbital-map.js
node _test_ring_fill.js orbital-map.js   # exits non-zero on regression
node _test_ring_fill.js index.html       # inlined copy must score identically
```

### 3. Deploy (JS-only change — no firmware reflash)

```bash
gzip -9 -c index.html > index.html.gz
curl -F "file=@index.html.gz" http://192.168.88.154/upload
```

### 4. Verify what the device actually serves

Don't trust "Upload complete" — diff it:

```bash
curl -s --compressed -o /tmp/served.html http://192.168.88.154/
cmp /tmp/served.html index.html && echo "IDENTICAL"
```

Then hard-reload (Cmd+Shift+R).

### 5. Inspecting the live renderer in the browser console

`window.orbitalMap` resolves to the **canvas element**, not the renderer — the id
`orbitalMap` shadows the script-scoped `let` on `window`. Use the bare name:

```javascript
orbitalMap instanceof OrbitalMapRenderer   // true
orbitalMap.rings.length                    // 560
orbitalMap.centerLon = -60; orbitalMap.render();
```

## Approaches that failed (do not retry)

Splitting at the seam, then closing fragments:

1. `closePath()` per subpath — closing line creates triangles
2. `fill("evenodd")` — unpredictable with self-intersecting paths
3. Subpaths with individual `closePath()` — same cross-map lines
4. No `closePath()`, fill open subpaths — `fill()` still closes implicitly
5. `lonOffset` ±360° — doesn't handle first↔last wrap
6. Conditional `closePath()` — open paths still fill as closed
7. Midpoint split — doesn't identify the antimeridian crossing
8. `fill("evenodd")` on the whole ring — "many horizontal lines"
9. Canvas-edge close (v1) — fills ocean on the wrong side of the coastline
10. `goUp` guard — `closePath()` fallback cut a 121px diagonal through North America
11. Winding formula (`isCW === goingDown`) — right for some rings, wrong for others
12. Point-in-ring test — unreliable near the ring boundary; still lost landmass

Mitigations that only reduced severity: separate fill/stroke paths (removed the
stroked edge lines — this part was correct and is retained in spirit), `wrapsGlobe`
+ `isFullWidth` priority, 2px inset.

**The lesson:** the bug was in the *decomposition*, not the closing rule. Twelve
attempts tuned the closing rule. Once the ring is kept whole, there is nothing to
decide.
