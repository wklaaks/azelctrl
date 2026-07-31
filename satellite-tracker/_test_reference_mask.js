// Independent ground-truth land mask, projection-agnostic.
// For each ring, a point P is inside iff:
//   (# of ring-edge crossings of P's meridian, at latitudes SOUTH of P) is odd
//   XOR (the south pole lies inside that ring)
// Ring edges are polyline segments in lat/lon (plate-carree) space, traversed
// the short way in longitude — exactly the polygon the renderer must fill.
// Exported as reference(centerLon, W, H) -> Uint8Array mask.
const norm = (d) => { while (d < -180) d += 360; while (d > 180) d -= 360; return d; };

function buildRingData(RINGS) {
  return RINGS.map((ring) => {
    const n = ring.length;
    // unwrapped longitudes
    const lons = new Array(n), lats = new Array(n);
    let u = ring[0][1], winding = 0;
    for (let i = 0; i < n; i++) {
      if (i > 0) { const d = norm(ring[i][1] - ring[i - 1][1]); u += d; winding += d; }
      lons[i] = u;
      lats[i] = ring[i][0];
    }
    winding += norm(ring[0][1] - ring[n - 1][1]);
    let sumLat = 0;
    for (let i = 0; i < n; i++) sumLat += lats[i];
    const encircles = Math.abs(winding) > 180;
    // south pole inside this ring only if it encircles a pole and sits in the south
    const southPoleInside = encircles && (sumLat / n) < 0;
    return { lats, lons, n, southPoleInside, encircles };
  });
}

// crossings of meridian L by ring, returning latitudes of crossings
function meridianCrossings(rd, L, out) {
  out.length = 0;
  const { lats, lons, n } = rd;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    let l1 = lons[i], l2 = lons[j];
    if (j === 0) { l2 = l1 + norm(lons[0] - lons[i]); } // closing edge, short way
    if (l1 === l2) continue;
    const lo = Math.min(l1, l2), hi = Math.max(l1, l2);
    // test every 360-shifted copy of L that falls in [lo, hi)
    let k = Math.ceil((lo - L) / 360);
    for (let Lk = L + k * 360; Lk < hi; Lk += 360) {
      if (Lk < lo) continue;
      // half-open rule on the lower endpoint to avoid double counting vertices
      if (Lk === hi) continue;
      const t = (Lk - l1) / (l2 - l1);
      if (t < 0 || t >= 1) continue;
      out.push(lats[i] + t * (lats[j] - lats[i]));
    }
  }
  return out;
}

function reference(RINGS, centerLon, W, H) {
  const rings = buildRingData(RINGS);
  const mask = new Uint8Array(W * H);
  const cr = [];
  for (let px = 0; px < W; px++) {
    // pixel center longitude
    const L = centerLon + ((px + 0.5) / W) * 360 - 180;
    for (const rd of rings) {
      meridianCrossings(rd, L, cr);
      if (!cr.length && !rd.southPoleInside) continue;
      const sorted = cr.slice().sort((a, b) => a - b);
      for (let py = 0; py < H; py++) {
        if (mask[py * W + px]) continue;
        const lat = 90 - ((py + 0.5) / H) * 180;
        // count crossings strictly south of lat
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

module.exports = { reference, norm };
