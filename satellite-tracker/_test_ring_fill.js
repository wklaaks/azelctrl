// Verifies the ACTUAL _drawRing() shipped in orbital-map.js.
// A fake canvas context records the fill paths, which are then rasterized with
// nonzero-winding scanlines and compared against an independent, projection-
// agnostic ground-truth land mask (_test_reference_mask.js).
// Usage: node _test_ring_fill.js
const fs = require('fs');
const path = require('path');
const DIR = __dirname;

const coast = fs.readFileSync(path.join(DIR, 'world_coastline.js'), 'utf8');
eval(coast.replace('const WORLD_OUTLINES', 'global.WORLD_OUTLINES'));
const RINGS = global.WORLD_OUTLINES;
const { reference } = require('./_test_reference_mask.js');

const W = 720, H = 360;

// --- Load the real _drawRing from orbital-map.js -----------------------------
function loadDrawRing(file) {
  const src = fs.readFileSync(path.join(DIR, file), 'utf8');
  const start = src.indexOf('  _drawRing(ring) {');
  if (start < 0) throw new Error('_drawRing not found in ' + file);
  // Walk braces to find the end of the method
  let depth = 0, i = src.indexOf('{', start), end = -1;
  for (; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}') { depth--; if (depth === 0) { end = i + 1; break; } }
  }
  if (end < 0) throw new Error('unbalanced braces in ' + file);
  const body = src.slice(start, end).replace(/^\s*_drawRing\(ring\)\s*\{/, '');
  // eslint-disable-next-line no-new-func
  return new Function('ring', body.slice(0, body.lastIndexOf('}')));
}

// --- Recording 2D context ----------------------------------------------------
class FakeCtx {
  constructor() { this.paths = []; this._cur = null; this.filled = []; }
  beginPath() { this.paths = []; this._cur = null; }
  moveTo(x, y) { this._cur = [[x, y]]; this.paths.push(this._cur); }
  lineTo(x, y) { if (this._cur) this._cur.push([x, y]); }
  closePath() { this._cur = null; }
  fill() { for (const p of this.paths) if (p.length > 2) this.filled.push(p); }
  stroke() { /* stroke does not affect the land mask */ }
}

// --- Rasterize recorded fill paths (nonzero winding) -------------------------
function rasterFromPolys(polys) {
  const mask = new Uint8Array(W * H);
  // Group all subpaths of one fill() together so nonzero winding unions them
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

function renderMask(drawRing, centerLon) {
  const groups = [];
  for (const ring of RINGS) {
    const ctx = new FakeCtx();
    const self = {
      ctx,
      centerLon,
      _logicalWidth: W,
      _logicalHeight: H,
      project(lat, lon) {
        let s = lon - this.centerLon;
        while (s < -180) s += 360;
        while (s > 180) s -= 360;
        return [((s + 180) / 360) * W, ((90 - lat) / 180) * H];
      },
    };
    drawRing.call(self, ring);
    if (ctx.filled.length) groups.push(ctx.filled);
  }
  return rasterFromPolys(groups);
}

function check(label, file) {
  const drawRing = loadDrawRing(file);
  console.log('\n=== ' + label + ' (' + file + ') ===');
  const counts = [];
  let worst = 0;
  for (const cl of [0, 30, -30, 60, 90, 150, 180, -60, -120, -179]) {
    const m = renderMask(drawRing, cl);
    const ref = reference(RINGS, cl, W, H);
    let land = 0, refLand = 0, oceanFilled = 0, landMissing = 0;
    for (let i = 0; i < m.length; i++) {
      land += m[i];
      refLand += ref[i];
      if (m[i] && !ref[i]) oceanFilled++;
      else if (!m[i] && ref[i]) landMissing++;
    }
    const wrong = oceanFilled + landMissing;
    if (wrong > worst) worst = wrong;
    counts.push(land);
    console.log('  centerLon=' + String(cl).padStart(4) +
      '  landPx=' + String(land).padStart(6) +
      '  refPx=' + String(refLand).padStart(6) +
      '  wrong=' + String(wrong).padStart(6) +
      ' (' + (100 * wrong / (W * H)).toFixed(2) + '%)' +
      '  oceanFilled=' + String(oceanFilled).padStart(5) +
      '  landMissing=' + String(landMissing).padStart(5));
  }
  const mn = Math.min(...counts), mx = Math.max(...counts);
  console.log('  worst error: ' + (100 * worst / (W * H)).toFixed(2) + '% of canvas');
  console.log('  land-area spread across pans: ' + (100 * (mx - mn) / mx).toFixed(2) + '%');
  return { worst, spread: (mx - mn) / mx };
}

const args = process.argv.slice(2);
const target = args[0] || 'orbital-map.js';
const res = check('current', target);

// Optional baseline comparison: node _test_ring_fill.js orbital-map.js <old-file>
if (args[1]) check('baseline', args[1]);

const WORST_TOL = 0.004;  // 0.4% of canvas — sub-pixel rasterizer disagreement
const SPREAD_TOL = 0.01;  // 1% land-area variation across pans
const okWorst = res.worst / (W * H) < WORST_TOL;
const okSpread = res.spread < SPREAD_TOL;
console.log('\n' + (okWorst && okSpread ? 'PASS' : 'FAIL') +
  ': worst=' + (100 * res.worst / (W * H)).toFixed(2) + '% (tol ' + (100 * WORST_TOL) + '%), ' +
  'spread=' + (100 * res.spread).toFixed(2) + '% (tol ' + (100 * SPREAD_TOL) + '%)');
process.exit(okWorst && okSpread ? 0 : 1);
