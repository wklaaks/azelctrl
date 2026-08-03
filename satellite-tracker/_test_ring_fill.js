// Verifies the ACTUAL _drawRing() shipped in the deployed index.html across all
// zoom levels and pan positions. A fake canvas context records the fill paths,
// which are rasterized with nonzero-winding scanlines and compared against an
// independent, projection-agnostic ground-truth land mask.
// Usage: node _test_ring_fill.js [file]   (default: index.html — the deployed SPA)
const fs = require('fs');
const path = require('path');
const DIR = __dirname;

const coast = fs.readFileSync(path.join(DIR, 'world_coastline.js'), 'utf8');
eval(coast.replace('const WORLD_COUNTRIES', 'global.WORLD_COUNTRIES'));
const RINGS = (global.WORLD_COUNTRIES || []).flatMap(c => c.r || []);

const W = 720, H = 360;

// --- Load the real _drawRing(ring, mode) from a file (current signature) ---
// Works for index.html (the deployed SPA: the method text is inlined verbatim,
// so extraction is identical to extracting from the standalone orbital-map.js).
function loadDrawRing(file) {
  const src = fs.readFileSync(path.join(DIR, file), 'utf8');
  let i0 = -1, j;
  // Match the METHOD declaration, not a call site like `this._drawRing(ring, 'fill');`
  while ((j = src.indexOf('  _drawRing(ring', i0 + 1)) !== -1) {
    if (/^ {2}_drawRing\(ring\) \{/.test(src.slice(j)) ||
        /^ {2}_drawRing\(ring, mode = 'both'\) \{/.test(src.slice(j))) { i0 = j; break; }
    i0 = j;
  }
  if (i0 < 0) throw new Error('_drawRing method not found in ' + file);
  let depth = 0, i = src.indexOf('{', i0), end = -1;
  for (; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}') { depth--; if (depth === 0) { end = i + 1; break; } }
  }
  if (end < 0) throw new Error('unbalanced braces in ' + file);
  const body = src.slice(i0, end)
    .replace(/^\s*_drawRing\(ring, mode = 'both'\)\s*\{/, '')
    .replace(/^\s*_drawRing\(ring\)\s*\{/, '');
  // eslint-disable-next-line no-new-func
  return new Function('ring', 'mode', body.slice(0, body.lastIndexOf('}')));
}

// --- Recording 2D context ---
class FakeCtx {
  constructor() { this.paths = []; this._cur = null; this.filled = []; }
  beginPath() { this.paths = []; this._cur = null; }
  moveTo(x, y) { this._cur = [[x, y]]; this.paths.push(this._cur); }
  lineTo(x, y) { if (this._cur) this._cur.push([x, y]); }
  closePath() { this._cur = null; }
  fill() { for (const p of this.paths) if (p.length > 2) this.filled.push(p); }
  stroke() { /* stroke does not affect the land mask */ }
}

// --- Rasterize recorded fill paths (nonzero winding) ---
function rasterFromPolys(polys) {
  const mask = new Uint8Array(W * H);
  for (const group of polys) {
    let mny = Infinity, mxy = -Infinity;
    for (const poly of group) for (const p of poly) {
      if (p[1] < mny) mny = p[1];
      if (p[1] > mxy) mxy = p[1];
    }
    const y0 = Math.max(0, Math.floor(mny)), y1 = Math.min(H - 1, Math.ceil(mxy));
    for (let py = y0; py <= y1; py++) {
      const yc = py + 0.5;
      const xsC = [];
      for (const poly of group) {
        for (let k = 0; k < poly.length; k++) {
          const a = poly[k], b = poly[(k + 1) % poly.length];
          if ((a[1] <= yc && b[1] > yc) || (b[1] <= yc && a[1] > yc)) {
            const t = (yc - a[1]) / (b[1] - a[1]);
            xsC.push([a[0] + t * (b[0] - a[0]), b[1] > a[1] ? 1 : -1]);
          }
        }
      }
      if (xsC.length < 2) continue;
      xsC.sort((p, q) => p[0] - q[0]);
      let wind = 0;
      for (let k = 0; k < xsC.length - 1; k++) {
        wind += xsC[k][1];
        if (wind !== 0) {
          const sx = Math.max(0, Math.ceil(xsC[k][0] - 0.5));
          const ex = Math.min(W - 1, Math.floor(xsC[k + 1][0] - 0.5));
          for (let px = sx; px <= ex; px++) mask[py * W + px] = 1;
        }
      }
    }
  }
  return mask;
}

// Replicate the shipped projection (longitude-centering, latitude-centering, zoom)
const proj = (self, lat, lon) => {
  const w = self._logicalWidth, h = self._logicalHeight;
  let s = lon - self.centerLon;
  while (s < -180) s += 360;
  while (s > 180) s -= 360;
  const vL = 360 / self.zoom, vLa = 180 / self.zoom;
  return [((s + vL / 2) / vL) * w, ((vLa / 2 - (lat - self.centerLat)) / vLa) * h];
};

function renderMask(drawRing, centerLat, centerLon, zoom) {
  const groups = [];
  for (const ring of RINGS) {
    const ctx = new FakeCtx();
    const self = { ctx, centerLat, centerLon, zoom, _logicalWidth: W, _logicalHeight: H, project: proj };
    drawRing.call(self, ring, 'fill');
    if (ctx.filled.length) groups.push(ctx.filled);
  }
  return rasterFromPolys(groups);
}

// --- Independent ground-truth mask (projection-agnostic spherical point-in-polygon) ---
// For each pixel (inverse-projected to lat/lon), count ring-edge crossings of its
// meridian south of the point; odd = inside, inverting when a ring encircles the
// south pole (Antarctica). Handles 360-periodicity of longitudes natively.
const norm = (d) => { while (d < -180) d += 360; while (d > 180) d -= 360; return d; };
function buildRingData(rings) {
  return rings.map((ring) => {
    const n = ring.length;
    const lons = new Array(n), lats = new Array(n);
    let u = ring[0][1], winding = 0;
    for (let i = 0; i < n; i++) {
      if (i > 0) { const d = norm(ring[i][1] - ring[i - 1][1]); u += d; winding += d; }
      lons[i] = u; lats[i] = ring[i][0];
    }
    winding += norm(ring[0][1] - ring[n - 1][1]);
    let sumLat = 0;
    for (let i = 0; i < n; i++) sumLat += lats[i];
    const encircles = Math.abs(winding) > 180;   // zoom-independent pole test
    return { lats, lons, n, southPoleInside: encircles && (sumLat / n) < 0 };
  });
}
function meridianCrossings(rd, L, out) {
  out.length = 0;
  const { lats, lons, n } = rd;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    let l1 = lons[i], l2 = lons[j];
    if (j === 0) l2 = l1 + norm(lons[0] - lons[i]);
    if (l1 === l2) continue;
    const lo = Math.min(l1, l2), hi = Math.max(l1, l2);
    let k = Math.ceil((lo - L) / 360);
    for (let Lk = L + k * 360; Lk < hi; Lk += 360) {
      if (Lk < lo) continue;
      if (Lk === hi) continue;
      const t = (Lk - l1) / (l2 - l1);
      if (t < 0 || t >= 1) continue;
      out.push(lats[i] + t * (lats[j] - lats[i]));
    }
  }
  return out;
}
function referenceZoomed(centerLat, centerLon, zoom) {
  const rings = buildRingData(RINGS);
  const vL = 360 / zoom, vLa = 180 / zoom;
  const mask = new Uint8Array(W * H);
  const cr = [];
  for (let px = 0; px < W; px++) {
    const L = centerLon + ((px + 0.5) / W) * vL - vL / 2;
    for (const rd of rings) {
      meridianCrossings(rd, L, cr);
      if (!cr.length && !rd.southPoleInside) continue;
      const sorted = cr.slice().sort((a, b) => a - b);
      for (let py = 0; py < H; py++) {
        if (mask[py * W + px]) continue;
        const lat = centerLat + vLa / 2 - ((py + 0.5) / H) * vLa;
        let c = 0;
        for (let i = 0; i < sorted.length; i++) { if (sorted[i] < lat) c++; else break; }
        let inside = (c % 2) === 1;
        if (rd.southPoleInside) inside = !inside;
        if (inside) mask[py * W + px] = 1;
      }
    }
  }
  return mask;
}

// --- Verify a single flat-ring (non-projection) claim is true for all rings? ---
function check(label, file, opts = {}) {
  const drawRing = loadDrawRing(file);
  console.log('\n=== ' + label + ' (' + file + ') ===');
  const zooms = opts.zooms || [1, 1.33, 2, 4];
  const centers = opts.centers || [[-33.9, 18.4], [0, 0], [-33.9, 179], [60, -120], [0, -120]];
  let worst = 0;
  const lons = opts.lons || [18.4, 0, -120, 179, -60];
  const lats = opts.lats || [-33.9, 0, 60];
  let allWrong = 0;
  const spreadMap = {};   // "zoom:clat" -> [landPx across centerLon]
  for (const zoom of zooms) {
    for (const clat of lats) {
      const key = zoom + ':' + clat;
      spreadMap[key] = [];
      for (const clon of lons) {
        const m = renderMask(drawRing, clat, clon, zoom);
        const ref = referenceZoomed(clat, clon, zoom);
        let land = 0, refLand = 0, oceanFilled = 0, landMissing = 0;
        for (let i = 0; i < m.length; i++) {
          land += m[i]; refLand += ref[i];
          if (m[i] && !ref[i]) oceanFilled++;
          else if (!m[i] && ref[i]) landMissing++;
        }
        const wrong = oceanFilled + landMissing;
        allWrong += wrong;
        if (wrong > worst) worst = wrong;
        spreadMap[key].push(land);
        console.log('  zoom=' + String(zoom).padEnd(5) +
          ' clat=' + String(clat).padEnd(6) + ' clon=' + String(clon).padEnd(6) +
          ' landPx=' + String(land).padEnd(6) + ' refPx=' + String(refLand).padEnd(6) +
          ' wrong=' + String(wrong).padEnd(5) + ' (' + (100 * wrong / (W * H)).toFixed(2) + '%)' +
          ' oceanFill=' + String(oceanFilled).padEnd(5) + ' landMiss=' + String(landMissing));
      }
    }
  }
  // Land-area spread across LONGITUDE pans — ONLY at zoom=1 (full globe), where
  // every pan shows all 360° of longitude, so land area MUST be pan-invariant
  // (the old bug swung it ±2.35%; a correct renderer is flat at ~0.1%). At
  // zoom>1 the visible window shows only part of the longitude range, so land
  // area legitimately varies with which longitudes happen to be in view (e.g. a
  // 50%-zoom window over the Pacific is mostly ocean) — spread is not a signal
  // there and must not be gated.
  let worstSpread = 0;
  for (const zoom of zooms) if (zoom === 1) for (const clat of lats) {
    const counts = spreadMap[zoom + ':' + clat];
    const mn = Math.min(...counts), mx = Math.max(...counts);
    const spread = (mx - mn) / mx;
    if (spread > worstSpread) worstSpread = spread;
    console.log('  zoom=' + String(zoom).padEnd(5) + ' clat=' + String(clat).padEnd(5) +
      ' land-area spread across lon-pans: ' + (100 * spread).toFixed(2) + '%');
  }
  console.log('  worst error: ' + (100 * worst / (W * H)).toFixed(2) + '% of canvas');
  console.log('  worst zoom=1 land-area spread: ' + (100 * worstSpread).toFixed(2) + '%');
  return { worst, spread: worstSpread, totalWrong: allWrong };
}

const args = process.argv.slice(2);
const target = args[0] || 'index.html';
const res = check('current (deployed index.html)', target);

if (args[1]) check('baseline', args[1]);

const WORST_TOL = 0.004;  // 0.4% of canvas — sub-pixel rasterizer disagreement
const SPREAD_TOL = 0.01;  // 1% land-area variation across views
const okWorst = res.worst / (W * H) < WORST_TOL;
const okSpread = res.spread < SPREAD_TOL;
console.log('\n' + (okWorst && okSpread ? 'PASS' : 'FAIL') +
  ': worst=' + (100 * res.worst / (W * H)).toFixed(2) + '% (tol ' + (100 * WORST_TOL) + '%), ' +
  'spread=' + (100 * res.spread).toFixed(2) + '% (tol ' + (100 * SPREAD_TOL) + '%)');
process.exit(okWorst && okSpread ? 0 : 1);
