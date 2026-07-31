// ============================================================
// Orbital Map — Canvas-based offline world map for AZELCtrl
// ============================================================
// Renders country polygons with per-country fill colors, satellite
// ground tracks, day/night terminator on an equirectangular Canvas.
// No external deps, fully offline.
// ============================================================

// --- Country color palette generator ---
// Deterministic color from country ID. Produces a muted map-style
// color with 360 possible hues, slight saturation/lightness variation.
function _countryColor(id) {
  // djb2 hash of the country ID string
  let hash = 5381;
  for (let i = 0; i < id.length; i++) {
    hash = ((hash << 5) + hash) + id.charCodeAt(i);
    hash = hash & hash; // 32-bit
  }
  const h = Math.abs(hash) % 360;
  const s = 48 + (Math.abs(hash >> 4) % 12);   // 48-59%
  const l = 24 + (Math.abs(hash >> 8) % 12);   // 24-35%
  return `hsl(${h}, ${s}%, ${l}%)`;
}

// --- Orbital Map Renderer ---
class OrbitalMapRenderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.centerLat = 0;
    this.centerLon = 0;
    this.zoom = 1; // 1 = full world, 2 = 2x zoom, etc.
    this.countries = []; // [{id, n, r: [[lat,lon],...]}, ...]
    this.rings = []; // fallback flat coastline rings (for backward compat)
    this.groundTrack = null; // { past: [[lat, lon], ...], future: [[lat, lon], ...] }
    this.satPos = null; // [lat, lon]
    this.stationPos = null; // [lat, lon]
    this.terminator = null; // [[lat, lon], ...]
    this.showTerminator = false;
    this.showGrid = true;
    this.showFootprint = true;
    this.footprintPoints = null; // [[lat, lon], ...] — visibility circle
    this.showMoon = false;
    this.moonPos = null; // [lat, lon]
    this.showSun = false;
    this.sunPos = null; // [lat, lon]
    // Offscreen canvas for caching the base layer (ocean + land)
    // Only redrawn when data or view changes — NOT on terminator/groundtrack toggles
    this._baseCanvas = null;
    this._baseDirty = true;
    this._initLines();
  }

  // Resize canvas to match container with proper device pixel ratio
  resize() {
    const container = this.canvas.parentElement;
    const rect = container.getBoundingClientRect();
    if (rect.width > 0) {
      const dpr = window.devicePixelRatio || 1;
      // Logical dimensions (CSS size, what project() uses)
      this._logicalWidth = rect.width;
      this._logicalHeight = rect.height || 280;  // Use container height, fallback 280
      // Set CSS size
      this.canvas.style.width = this._logicalWidth + 'px';
      this.canvas.style.height = this._logicalHeight + 'px';
      // Set actual pixel buffer size with DPR
      this.canvas.width = this._logicalWidth * dpr;
      this.canvas.height = this._logicalHeight * dpr;
      // Reset transform and scale to DPR so drawing uses logical coords
      this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      // Invalidate base cache on resize
      this._baseDirty = true;
      this._baseCanvas = null;
      return true;
    }
    return false;
  }

  _initLines() {
    // Pre-allocate line buffers for performance
    this._lineBuffers = {};
  }

  // Equirectangular projection: lat/lon → canvas x/y
  // Returns logical coordinates (matches CSS size, not physical pixel buffer)
  project(lat, lon) {
    // Logical canvas dimensions
    const w = this._logicalWidth;
    const h = this._logicalHeight;
    // Normalize longitude relative to center to [-180, 180] range for world wrapping
    let lonShift = lon - this.centerLon;
    while (lonShift < -180) lonShift += 360;
    while (lonShift > 180) lonShift -= 360;
    // Map to canvas coordinates: -180 → left edge, +180 → right edge
    const x = ((lonShift + 180) / 360) * w;
    const y = ((90 - lat) / 180) * h;
    return [x, y];
  }

  // Inverse projection: canvas x/y → lat/lon
  unproject(x, y) {
    const lon = (x / this.canvas.width) * 360 + this.centerLon - 180 * (this.zoom - 1);
    const lat = 90 - (y / this.canvas.height) * 180;
    return [lat, lon];
  }

  // Set country data (array of {id, n, r: [[lat,lon],...]})
  setCountries(countries) {
    this.countries = countries;
    this.rings = []; // clear fallback when using country data
    this._baseDirty = true;
  }

  // Set coastline data (fallback: array of rings, each ring is [[lat, lon], ...])
  setCoastline(rings) {
    this.rings = rings;
    this._baseDirty = true;
  }

  // Set ground track
  setGroundTrack(past, future, orbitSegments) {
    this.groundTrack = { past, future, orbitSegments };
  }

  // Set satellite position
  setSatPos(lat, lon) {
    this.satPos = [lat, lon];
  }

  // Set ground station position
  setStationPos(lat, lon) {
    this.stationPos = [lat, lon];
  }

  // Set terminator line
  setTerminator(result, show) {
    this.terminator = result.points || result;
    this._sunLonDeg = result.sunLonDeg || 0;
    this.showTerminator = show;
  }

  // Center map on a lat/lon position
  centerOn(lat, lon) {
    this.centerLat = lat;
    this.centerLon = lon;
    this._baseDirty = true;
  }

  // Set show terminator flag (does NOT invalidate base — overlay only)
  setShowTerminator(show) {
    this.showTerminator = show;
  }

  // Set satellite footprint (visibility circle)
  setFootprint(satLat, satLon, altKm) {
    if (!altKm || altKm <= 0.1) { // skip NaN, null, zero, micro-altitudes
      this.footprintPoints = null;
      return;
    }
    if (satLat == null || satLon == null || isNaN(satLat) || isNaN(satLon)) {
      this.footprintPoints = null;
      return;
    }
    const R = 6371; // Earth radius km
    const ratio = R / (R + altKm);
    // Clamp to [-1, 1] to prevent NaN from floating-point overshoot
    const theta = Math.acos(Math.max(-1, Math.min(1, ratio))); // central angle in radians
    const points = [];
    const steps = 360; // 1° increments for smooth circle
    const lat0 = satLat * Math.PI / 180;
    const lon0 = satLon * Math.PI / 180;
    const sinTheta = Math.sin(theta);
    const cosTheta = Math.cos(theta);
    for (let i = 0; i < steps; i++) {
      const bearing = (i / steps) * 2 * Math.PI;
      // Clamp asin argument to [-1, 1] to prevent NaN at poles
      const asinArg = Math.max(-1, Math.min(1,
        Math.sin(lat0) * cosTheta + Math.cos(lat0) * sinTheta * Math.cos(bearing)
      ));
      const lat1 = Math.asin(asinArg);
      const lon1 = lon0 + Math.atan2(
        Math.sin(bearing) * sinTheta * Math.cos(lat0),
        cosTheta - Math.sin(lat0) * Math.sin(lat1)
      );
      points.push([lat1 * 180 / Math.PI, lon1 * 180 / Math.PI]);
    }
    this.footprintPoints = points;
  }

  // Toggle footprint visibility
  setShowFootprint(show) {
    this.showFootprint = show;
  }

  // Toggle grid visibility
  setShowGrid(show) {
    this.showGrid = show;
  }

  // Set sun position and toggle
  setSunPos(lat, lon) {
    this.sunPos = [lat, lon];
  }
  setShowSun(show) {
    this.showSun = show;
  }

  // Set moon position and toggle
  setMoonPos(lat, lon) {
    this.moonPos = [lat, lon];
  }
  setShowMoon(show) {
    this.showMoon = show;
  }

  // Render the full map
  render() {
    const w = this._logicalWidth || 800;
    const h = this._logicalHeight || 280;
    const ctx = this.ctx;
    const dpr = window.devicePixelRatio || 1;

    // --- Base layer (ocean + land) — cached in offscreen canvas ----------
    // Only redrawn when data or view changes (pan, zoom, resize, data load).
    // Terminator toggle, ground track updates, and marker updates skip this.
    this._ensureBaseCanvas(w, h, dpr);
    if (this._baseDirty) {
      this._renderBase();
      this._baseDirty = false;
    }

    // Blit cached base to main canvas
    ctx.drawImage(this._baseCanvas, 0, 0, w * dpr, h * dpr, 0, 0, w, h);

    // 3. Draw day/night terminator (overlay — cheap, always redrawn)
    if (this.showTerminator && this.terminator && this.terminator.length > 0) {
      this._drawTerminator();
    }

    // 3b. Draw satellite footprint (visibility circle)
    if (this.showFootprint && this.footprintPoints && this.footprintPoints.length > 0) {
      this._drawFootprint();
    }

    // 4. Draw ground track
    if (this.groundTrack) {
      // Draw orbit segments with different colors
      if (this.groundTrack.orbitSegments && this.groundTrack.orbitSegments.length > 0) {
        for (const seg of this.groundTrack.orbitSegments) {
          const colorIdx = ((seg.orbitNum % 5) + 5) % 5;
          const color = ORBIT_COLORS[colorIdx];
          ctx.strokeStyle = color;
          ctx.lineWidth = 2;
          ctx.setLineDash([]);
          ctx.globalAlpha = 1;
          ctx.beginPath();
          for (let i = 0; i < seg.points.length; i++) {
            const [x, y] = this.project(seg.points[i][0], seg.points[i][1]);
            if (i === 0) {
              ctx.moveTo(x, y);
            } else {
              const prevX = this.project(seg.points[i-1][0], seg.points[i-1][1])[0];
              if (Math.abs(x - prevX) > w * 0.8) ctx.moveTo(x, y);
              else ctx.lineTo(x, y);
            }
          }
          ctx.stroke();
        }
        ctx.setLineDash([]);
        ctx.globalAlpha = 1;
      }
    }

    // 5. Draw grid lines
    if (this.showGrid) {
      this._drawGrid();
    }

    // 6. Draw markers
    if (this.stationPos) {
      this._drawMarker(this.stationPos[0], this.stationPos[1], '#22c55e', 'home', 6);
    }
    if (this.satPos) {
      this._drawMarker(this.satPos[0], this.satPos[1], '#38bdf8', 'diamond', 8);
    }

    // 7. Draw celestial bodies
    if (this.showSun && this.sunPos) {
      this._drawCelestialBody(this.sunPos[0], this.sunPos[1], '#fbbf24', '☀️');
    }
    if (this.showMoon && this.moonPos) {
      this._drawCelestialBody(this.moonPos[0], this.moonPos[1], '#c0c0c0', '🌙');
    }
  }

  _drawRing(ring, mode = 'both') {
    const ctx = this.ctx;
    const n = ring.length;
    if (n < 3) return;

    const w = this._logicalWidth || 800;
    const h = this._logicalHeight || 280;

    // --- Longitude unwrapping ---------------------------------------------
    // The old approach split each ring wherever its projected x jumped across
    // the map seam, then guessed how to close each fragment (via the canvas
    // edge, the poles, or closePath). Every guess was wrong for some ring at
    // some centerLon, producing either triangular gaps or ocean filled with
    // land colour.
    //
    // Instead, walk the ring in CONTINUOUS unwrapped longitude: accumulate the
    // shortest step between consecutive points so the polygon never jumps the
    // seam. The result is one unbroken polygon that may extend outside [0, w].
    // Draw it up to three times, offset by -w / 0 / +w, so whatever straddles
    // the seam reappears on the opposite side. No splitting, no close-mode
    // heuristics — every ring is filled as the single closed polygon it is.
    const xs = new Array(n);
    const ys = new Array(n);
    let u = ring[0][1] - this.centerLon;
    while (u < -180) u += 360;
    while (u > 180) u -= 360;
    let minU = u, maxU = u, sumLat = 0;
    const u0 = u;
    for (let i = 0; i < n; i++) {
      if (i > 0) {
        // Shortest longitude step from the previous point
        let d = ring[i][1] - ring[i - 1][1];
        while (d < -180) d += 360;
        while (d > 180) d -= 360;
        u += d;
      }
      if (u < minU) minU = u;
      if (u > maxU) maxU = u;
      sumLat += ring[i][0];
      xs[i] = ((u + 180) / 360) * w;
      ys[i] = ((90 - ring[i][0]) / 180) * h;
    }
    // Shortest closing step from the last point back to the first
    let dClose = ring[0][1] - ring[n - 1][1];
    while (dClose < -180) dClose += 360;
    while (dClose > 180) dClose -= 360;

    // Total longitude wound by the ring. A ring that winds a full turn
    // encircles a pole (Antarctica) — it has no last->first edge in unwrapped
    // space, so it must be closed through the pole, off-canvas, and never with
    // a line across the map.
    const uEnd = u + dClose;
    const winding = uEnd - u0;
    const enclosesPole = Math.abs(winding) > 180;
    const polarY = (sumLat / n) < 0 ? h + 8 : -8;
    const xEnd = ((uEnd + 180) / 360) * w;
    if (enclosesPole) {
      // The closure reaches the wrapped-around first point, one full turn out
      if (uEnd < minU) minU = uEnd;
      if (uEnd > maxU) maxU = uEnd;
    }

    // Copies to draw: cull any that fall entirely off-canvas
    const shifts = [];
    for (let s = -1; s <= 1; s++) {
      if (minU + s * 360 <= 180 && maxU + s * 360 >= -180) shifts.push(s * w);
    }
    if (shifts.length === 0) return;

    // Trace the ring (and, for pole-encirclers, the closure through the pole)
    const trace = (dx, closeThroughPole) => {
      ctx.moveTo(xs[0] + dx, ys[0]);
      for (let i = 1; i < n; i++) ctx.lineTo(xs[i] + dx, ys[i]);
      if (closeThroughPole) {
        // Continue to the wrapped first point, then out past the pole and back.
        // Spans exactly one full turn (w px), so seam copies abut with no gap.
        ctx.lineTo(xEnd + dx, ys[0]);
        ctx.lineTo(xEnd + dx, polarY);
        ctx.lineTo(xs[0] + dx, polarY);
      }
    };

    // --- Fill: all seam copies in one path, one fill -----------------------
    // Same-winding subpaths union cleanly under the nonzero rule, so abutting
    // copies leave no hairline at the seam.
    if (mode !== 'stroke') {
      ctx.beginPath();
      for (const dx of shifts) {
        trace(dx, enclosesPole);
        ctx.closePath();
      }
      ctx.fill();
    }

    // --- Stroke: coastline only, never the polar closure -------------------
    if (mode !== 'fill') {
      ctx.beginPath();
      for (const dx of shifts) {
        trace(dx, false);
        // Pole-encirclers have no real last->first edge; closing it would draw a
        // line straight across the map.
        if (!enclosesPole) ctx.closePath();
      }
      ctx.stroke();
    }
  }

  // Ensure the offscreen base canvas exists and is sized correctly
  _ensureBaseCanvas(w, h, dpr) {
    const needW = w * dpr;
    const needH = h * dpr;
    if (!this._baseCanvas || this._baseCanvas.width !== needW || this._baseCanvas.height !== needH) {
      this._baseCanvas = document.createElement('canvas');
      this._baseCanvas.width = needW;
      this._baseCanvas.height = needH;
      this._baseDirty = true;
    }
  }

  // Render the base layer (ocean + land) to the offscreen canvas
  _renderBase() {
    const bc = this._baseCanvas;
    const dpr = window.devicePixelRatio || 1;
    const w = bc.width / dpr;
    const h = bc.height / dpr;

    // Temporarily redirect this.ctx to the offscreen canvas so _drawRing
    // (which reads this.ctx) draws to the cache instead of the main canvas.
    const mainCtx = this.ctx;
    this.ctx = bc.getContext('2d');

    // Apply same DPR transform as main canvas
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    // 1. Fill ocean background
    this.ctx.fillStyle = '#0c1929';
    this.ctx.fillRect(0, 0, w, h);

    // 2. Draw land masses
    if (this.countries && this.countries.length > 0) {
      // Fill each country with its deterministic color
      for (const country of this.countries) {
        if (!country.r || country.r.length === 0) continue;
        this.ctx.fillStyle = _countryColor(country.id);
        for (const ring of country.r) {
          this._drawRing(ring, 'fill');
        }
      }
      // Stroke all country boundaries
      this.ctx.strokeStyle = '#2a4a2a';
      this.ctx.lineWidth = 0.5;
      for (const country of this.countries) {
        if (!country.r || country.r.length === 0) continue;
        for (const ring of country.r) {
          this._drawRing(ring, 'stroke');
        }
      }
    } else if (this.rings && this.rings.length > 0) {
      // Fallback: flat coastline with single color
      this.ctx.fillStyle = '#1a2a1a';
      this.ctx.strokeStyle = '#2a4a2a';
      this.ctx.lineWidth = 0.5;
      for (const ring of this.rings) {
        this._drawRing(ring, 'both');
      }
    }

    // Restore main canvas context
    this.ctx = mainCtx;
  }

  _drawTerminator() {
    const ctx = this.ctx;
    if (!this.terminator || this.terminator.length < 2) return;

    const w = this._logicalWidth || 800;
    const h = this._logicalHeight || 280;

    // For each latitude strip, fill the night region between sunset and sunrise
    ctx.save();
    ctx.globalAlpha = 0.35;
    ctx.fillStyle = '#000';

    for (let i = 0; i < this.terminator.length; i++) {
      const pt = this.terminator[i];
      const [sx, sy] = this.project(pt.lat, pt.lonSunset);
      const [rx, ry] = this.project(pt.lat, pt.lonSunrise);

      // Height of this strip — extend to midpoint between this and next latitude step
      let stripH = 4;
      if (i + 1 < this.terminator.length) {
        const [, nextY] = this.project(this.terminator[i + 1].lat, pt.lonSunset);
        stripH = Math.abs(nextY - sy) + 1;  // +1 to ensure no gaps
      }

      // Night goes eastward from sunset to sunrise
      // If sunset_x < sunrise_x: fill between them
      // If sunset_x > sunrise_x: night wraps around map edge
      if (sx < rx) {
        ctx.fillRect(sx, sy, rx - sx, stripH);
      } else {
        ctx.fillRect(sx, sy, w - sx, stripH);
        ctx.fillRect(0, sy, rx, stripH);
      }
    }

    ctx.restore();

    // Draw terminator lines
    ctx.save();
    ctx.strokeStyle = 'rgba(251, 191, 36, 0.7)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);

    // Sunset line
    ctx.beginPath();
    for (let i = 0; i < this.terminator.length; i++) {
      const [x, y] = this.project(this.terminator[i].lat, this.terminator[i].lonSunset);
      if (i === 0) ctx.moveTo(x, y);
      else {
        const prevX = this.project(this.terminator[i-1].lat, this.terminator[i-1].lonSunset)[0];
        if (Math.abs(x - prevX) > w * 0.8) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
    }
    ctx.stroke();

    // Sunrise line
    ctx.beginPath();
    for (let i = 0; i < this.terminator.length; i++) {
      const [x, y] = this.project(this.terminator[i].lat, this.terminator[i].lonSunrise);
      if (i === 0) ctx.moveTo(x, y);
      else {
        const prevX = this.project(this.terminator[i-1].lat, this.terminator[i-1].lonSunrise)[0];
        if (Math.abs(x - prevX) > w * 0.8) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
    }
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.restore();
  }

  _drawFootprint() {
    const ctx = this.ctx;
    ctx.save();

    // Fill with translucent green — the area where satellite is visible
    ctx.fillStyle = 'rgba(34, 197, 94, 0.10)';
    this._drawRing(this.footprintPoints, 'fill');

    // Dashed outline
    ctx.strokeStyle = 'rgba(34, 197, 94, 0.6)';
    ctx.lineWidth = 1.5;
    ctx.setLineDash([5, 4]);
    this._drawRing(this.footprintPoints, 'stroke');
    ctx.setLineDash([]);

    ctx.restore();
  }

  _drawGrid() {
    const ctx = this.ctx;
    const w = this.canvas.width;
    const h = this.canvas.height;

    ctx.strokeStyle = 'rgba(88, 166, 255, 0.1)';
    ctx.lineWidth = 0.5;
    ctx.fillStyle = 'rgba(88, 166, 255, 0.4)';
    ctx.font = '9px monospace';

    // Latitude lines every 30°
    for (let lat = -90; lat <= 90; lat += 30) {
      const [, y] = this.project(lat, 0);
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();

      // Label
      const label = `${Math.abs(lat)}°${lat >= 0 ? 'N' : 'S'}`;
      ctx.fillText(label, 2, y - 2);
    }

    // Longitude lines every 60°
    for (let lon = -180; lon < 180; lon += 60) {
      const [x] = this.project(0, lon);
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
      ctx.stroke();

      // Label
      const label = `${Math.abs(lon)}°${lon >= 0 ? 'E' : 'W'}`;
      ctx.fillText(label, x + 2, h - 2);
    }

    // Equator and prime meridian
    ctx.strokeStyle = 'rgba(88, 166, 255, 0.25)';
    ctx.lineWidth = 1;
    const [eqX] = this.project(0, 0);
    const [, eqY] = this.project(0, 0);

    ctx.beginPath();
    ctx.moveTo(0, eqY);
    ctx.lineTo(w, eqY);
    ctx.stroke();

    ctx.beginPath();
    ctx.moveTo(eqX, 0);
    ctx.lineTo(eqX, h);
    ctx.stroke();
  }

  _drawMarker(lat, lon, color, shape, size) {
    const ctx = this.ctx;
    const [x, y] = this.project(lat, lon);

    // Glow
    ctx.save();
    ctx.shadowColor = color;
    ctx.shadowBlur = 8;

    if (shape === 'diamond') {
      // Diamond shape for satellite
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.moveTo(x, y - size);
      ctx.lineTo(x + size, y);
      ctx.lineTo(x, y + size);
      ctx.lineTo(x - size, y);
      ctx.closePath();
      ctx.fill();

      // Outline
      ctx.strokeStyle = '#fff';
      ctx.lineWidth = 1;
      ctx.stroke();
    } else if (shape === 'home') {
      // Circle with dot for ground station
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.arc(x, y, size, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#000';
      ctx.beginPath();
      ctx.arc(x, y, size * 0.4, 0, Math.PI * 2);
      ctx.fill();
    }

    ctx.restore();
  }

  _drawCelestialBody(lat, lon, color, icon) {
    const ctx = this.ctx;
    const [x, y] = this.project(lat, lon);
    ctx.save();
    // Glow
    ctx.shadowColor = color;
    ctx.shadowBlur = 12;
    // Colored circle
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(x, y, 7, 0, Math.PI * 2);
    ctx.fill();
    // White outline
    ctx.shadowBlur = 0;
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 1.5;
    ctx.stroke();
    ctx.restore();
  }
}

// --- Ground Track Computation ---

// Propagate satellite and return [lat, lon] in degrees
function getSatGroundPosition(satrec, date) {
  const posVel = satellite.propagate(satrec, date);
  if (!posVel.position) return null;

  const pos = posVel.position;
  const gmst = satellite.gstime(date);

  // Convert ECI to lat/lon
  const x = pos.x;
  const y = pos.y;
  const z = pos.z;

  // WGS84
  const a = 6378.137;
  const f = 1 / 298.257223563;
  const e2 = 2 * f - f * f;

  // Longitude from ECI + GMST
  let lon = Math.atan2(y, x) - gmst;
  // Normalize to -180..180
  while (lon > Math.PI) lon -= 2 * Math.PI;
  while (lon < -Math.PI) lon += 2 * Math.PI;

  // Latitude from ECI (geocentric, close enough for map display)
  const rho = Math.sqrt(x * x + y * y);
  let lat = Math.atan2(z, rho);

  // Iterative geodetic latitude correction
  for (let i = 0; i < 3; i++) {
    const N = a / Math.sqrt(1 - e2 * Math.sin(lat) * Math.sin(lat));
    lat = Math.atan2(z + N * e2 * Math.sin(lat), rho);
  }

  // Altitude (for display) — height above WGS84 ellipsoid
  const N = a / Math.sqrt(1 - e2 * Math.sin(lat) * Math.sin(lat));
  const R_ellip = Math.sqrt((N * Math.cos(lat))**2 + (N * (1 - e2) * Math.sin(lat))**2);
  const alt = Math.sqrt(x * x + y * y + z * z) - R_ellip;

  return {
    lat: lat * 180 / Math.PI,
    lon: lon * 180 / Math.PI,
    alt: alt // km above WGS84 ellipsoid
  };
}

// Orbit track colors — 5 distinct colors visible against dark background
const ORBIT_COLORS = ['#ef4444', '#38bdf8', '#fbbf24', '#a78bfa', '#34d399'];

// Generate ground track points for a satellite (future only), split into orbit segments.
// Adjacent segments share their boundary point so there are no gaps in the rendered track.
function generateGroundTrack(satrec, durationMinutes = 180, stepMinutes = 2) {
  const now = new Date();
  const futurePoints = [];
  const orbitSegments = []; // [{orbitNum, points: [[lat, lon], ...]}, ...]

  // Compute orbit number from mean anomaly at epoch
  // satrec.no = mean motion (rad/min), satrec.mo = mean anomaly at epoch (rad)
  // satrec.jdsatepoch = Julian date of epoch (days)
  const epochMs = (satrec.jdsatepoch * 86400 * 1000) || now.getTime();
  const no = satrec.no || 0; // rad/min
  const mo = satrec.mo || 0; // rad

  // Compute orbit number at 'now'
  const elapsedMin = (now.getTime() - epochMs) / (1000 * 60);
  const meanMotionAtNow = mo + no * elapsedMin;
  const orbitNumNow = Math.floor(meanMotionAtNow / (2 * Math.PI));

  // Future track — start from now (i=0) so there's no gap between the
  // satellite position marker and the first orbit segment
  let currentOrbit = orbitNumNow;
  let currentSegment = [];

  // Include the current position as the first point
  const nowPos = getSatGroundPosition(satrec, now);
  if (nowPos) {
    currentSegment.push([nowPos.lat, nowPos.lon]);
    futurePoints.push([nowPos.lat, nowPos.lon]);
  }

  for (let i = stepMinutes; i <= durationMinutes; i += stepMinutes) {
    const time = new Date(now.getTime() + i * 60 * 1000);
    const pos = getSatGroundPosition(satrec, time);
    if (pos) {
      futurePoints.push([pos.lat, pos.lon]);
      // Compute orbit number at this time
      const elapsed = (time.getTime() - epochMs) / (1000 * 60);
      const meanAnomaly = mo + no * elapsed;
      const orbitNum = Math.floor(meanAnomaly / (2 * Math.PI));
      if (orbitNum !== currentOrbit) {
        // Push the completed segment (must have at least 2 points)
        if (currentSegment.length > 1) {
          orbitSegments.push({ orbitNum: currentOrbit, points: currentSegment });
        }
        currentOrbit = orbitNum;
        // Start new segment with the LAST point of the previous segment
        // so there's no gap between orbit colours
        currentSegment = currentSegment.length > 0
          ? [currentSegment[currentSegment.length - 1]]
          : [];
      }
      currentSegment.push([pos.lat, pos.lon]);
    }
  }
  // Push the last segment
  if (currentSegment.length > 1) {
    orbitSegments.push({ orbitNum: currentOrbit, points: currentSegment });
  }

  return { future: futurePoints, orbitSegments };
}

// Compute day/night terminator line
function computeTerminator(date) {
  // Julian date
  const y = date.getUTCFullYear();
  const m = date.getUTCMonth() + 1;
  const d = date.getUTCDate() + date.getUTCHours() / 24 + date.getUTCMinutes() / 1440;
  let A = Math.floor(y);
  let B = 2 - A + Math.floor(A / 4);
  const JD = Math.floor(365.25 * (y + 4716)) + Math.floor(30.6001 * (m + 1)) + d + B - 1524.5;

  // Julian century
  const T = (JD - 2451545.0) / 36525;

  // Sun's geometric mean longitude (degrees)
  const L0 = (280.46646 + T * (36000.76983 + 0.0003032 * T)) % 360;
  // Sun's mean anomaly (degrees)
  const M = (357.52911 + T * 35999.05029) % 360;
  // Eccentricity
  const e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);

  // Equation of center
  const Mrad = M * Math.PI / 180;
  const C = Math.sin(Mrad) * (1.914602 - T * (0.004817 + 0.000014 * T))
          + Math.sin(2 * Mrad) * (0.019993 - 0.000101 * T)
          + Math.sin(3 * Mrad) * 0.000289;

  // Sun's true longitude (ecliptic, degrees)
  const sunTrueLon = L0 + C;
  const sunTrueLonRad = sunTrueLon * Math.PI / 180;

  // Obliquity of ecliptic (degrees)
  const obliquity = 23.4393 - 0.0000004 * T;
  const obliquityRad = obliquity * Math.PI / 180;

  // Sun's right ascension and declination
  const sinDec = Math.sin(sunTrueLonRad) * Math.sin(obliquityRad);
  const sunDec = Math.asin(sinDec);
  const sunDecDeg = sunDec * 180 / Math.PI;
  const cosDec = Math.cos(sunDec);
  const sunRA = Math.atan2(
    Math.sin(sunTrueLonRad) * Math.cos(obliquityRad),
    Math.cos(sunTrueLonRad)
  );
  const sunRADeg = sunRA * 180 / Math.PI;

  // GMST in degrees
  const T0 = T;
  const GMSTdeg = (280.46061837 + 360.98564736629 * (JD - 2451545.0) + 0.000387933 * T0 * T0 - 0.00000001507 * T0 * T0 * T0) % 360;

  // Terminator: at each latitude, the terminator is where local hour angle = ±HA
  // HA = acos(-tan(lat) * tan(sun_dec))
  // Local sidereal time = GMST + longitude
  // HA = LST - sun_RA => longitude = HA + sun_RA - GMST
  const points = [];
  for (let lat = 90; lat >= -90; lat -= 2) {
    const latRad = lat * Math.PI / 180;
    const cosHA = -Math.tan(latRad) * Math.tan(sunDec);

    if (cosHA > 1 || cosHA < -1) {
      // Polar day or night — no terminator here
      continue;
    }

    const haDeg = Math.acos(cosHA) * 180 / Math.PI;

    // Sunset terminator (sunset line — day to night)
    let lonSunset = sunRADeg + haDeg - GMSTdeg;
    // Sunrise terminator (sunrise line — night to day)
    let lonSunrise = sunRADeg - haDeg - GMSTdeg;

    // Normalize
    while (lonSunset > 180) lonSunset -= 360;
    while (lonSunset < -180) lonSunset += 360;
    while (lonSunrise > 180) lonSunrise -= 360;
    while (lonSunrise < -180) lonSunrise += 360;

    points.push({ lat, lonSunset, lonSunrise });
  }

  // Sun's sub-point longitude (degrees)
  const sunLonDeg = (sunRADeg - GMSTdeg + 540) % 360 - 180;

  return { points, sunLonDeg };
}

// --- Map Integration ---

let orbitalMap = null; // OrbitalMapRenderer instance
let mapUpdateInterval = null;

function initOrbitalMap() {
  const canvas = document.getElementById('orbitalMap');
  if (!canvas) return;

  orbitalMap = new OrbitalMapRenderer(canvas);

  // Use ResizeObserver to handle tab visibility changes and window resize
  const resizeObserver = new ResizeObserver(() => {
    if (orbitalMap && orbitalMap.resize()) {
      orbitalMap.render();
    }
  });
  resizeObserver.observe(canvas.parentElement);

  // Initial resize (may be skipped if container is hidden — ResizeObserver will catch it)
  orbitalMap.resize();

  // Load map data from SPIFFS — the file at /world_coastline.js contains
    // either WORLD_COUNTRIES (country-polygon data with colors) or
    // WORLD_OUTLINES (flat coastline rings, fallback).
    fetch('/world_coastline.js')
      .then(r => {
        if (!r.ok) throw new Error('world_coastline.js not found on SPIFFS');
        return r.text();
      })
      .then(text => {
        const script = document.createElement('script');
        script.textContent = text;
        document.head.appendChild(script);
        if (typeof WORLD_COUNTRIES !== 'undefined') {
          orbitalMap.setCountries(WORLD_COUNTRIES);
        } else if (typeof WORLD_OUTLINES !== 'undefined') {
          orbitalMap.setCoastline(WORLD_OUTLINES);
        }
        orbitalMap.setStationPos(stationLocation.lat, stationLocation.lon);
        orbitalMap.centerOn(stationLocation.lat, stationLocation.lon);
        orbitalMap.render();
      })
      .catch(e => {
        console.warn('Orbital map: map data not available:', e.message);
        const container = document.getElementById('orbitalMapContainer');
        if (container) {
          const msg = document.createElement('div');
          msg.className = 'banner warn';
          msg.style.cssText = 'margin:12px;padding:16px;text-align:center;';
          msg.innerHTML = `
            <strong>World map data not found</strong><br>
            <span style="font-size:0.8rem;">Upload <code>world_coastline.js.gz</code> via the <strong>Manage</strong> tab to enable the orbital map.</span>
          `;
          container.querySelector('.orbital-map-footer').after(msg);
        }
      });

  // Map controls
  const resetBtn = document.getElementById('mapResetBtn');
  const centerSatBtn = document.getElementById('mapCenterSatBtn');
  if (resetBtn) {
    resetBtn.addEventListener('click', () => {
      if (orbitalMap) {
        orbitalMap.centerOn(stationLocation.lat, stationLocation.lon);
        orbitalMap.zoom = 1;
        orbitalMap.render();
      }
    });
  }
  if (centerSatBtn) {
    centerSatBtn.addEventListener('click', () => {
      if (orbitalMap && orbitalMap.satPos) {
        orbitalMap.centerOn(orbitalMap.satPos[0], orbitalMap.satPos[1]);
        orbitalMap.render();
      }
    });
  }

  // Pan with mouse drag
  let isDragging = false;
  let dragStartX, dragStartY;
  let dragStartCenterLon, dragStartCenterLat;

  canvas.addEventListener('mousedown', (e) => {
    isDragging = true;
    dragStartX = e.clientX;
    dragStartY = e.clientY;
    dragStartCenterLon = orbitalMap.centerLon;
    dragStartCenterLat = orbitalMap.centerLat;
    e.preventDefault();
  });

  canvas.addEventListener('mousemove', (e) => {
    if (!isDragging || !orbitalMap) return;
    const dx = e.clientX - dragStartX;
    const dy = e.clientY - dragStartY;
    // Convert pixel movement to degrees
    const dLon = -(dx / canvas.width) * 360 / orbitalMap.zoom;
    const dLat = (dy / canvas.height) * 180 / orbitalMap.zoom;
    orbitalMap.centerLon = dragStartCenterLon + dLon;
    orbitalMap.centerLat = dragStartCenterLat + dLat;
    orbitalMap._baseDirty = true;
    orbitalMap.render();
  });

  canvas.addEventListener('mouseup', () => { isDragging = false; });
  canvas.addEventListener('mouseleave', () => { isDragging = false; });

  // Zoom with mouse wheel
  canvas.addEventListener('wheel', (e) => {
    if (!orbitalMap) return;
    e.preventDefault();
    const oldZoom = orbitalMap.zoom;
    if (e.deltaY < 0) {
      orbitalMap.zoom = Math.min(8, orbitalMap.zoom * 1.3);
    } else {
      orbitalMap.zoom = Math.max(1, orbitalMap.zoom / 1.3);
    }
    // Zoom towards cursor position
    const rect = canvas.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;
    const [latBefore, lonBefore] = orbitalMap.unproject(mx, my);
    const [latAfter, lonAfter] = orbitalMap.unproject(mx, my);
    orbitalMap.centerLat -= (latAfter - latBefore);
    orbitalMap.centerLon -= (lonAfter - lonBefore);
    orbitalMap._baseDirty = true;
    orbitalMap.render();
  }, { passive: false });

  // Touch support for pan and pinch-zoom
  let touchStartDist = null;
  let touchStartZoom = 1;

  canvas.addEventListener('touchstart', (e) => {
    if (e.touches.length === 1) {
      isDragging = true;
      dragStartX = e.touches[0].clientX;
      dragStartY = e.touches[0].clientY;
      dragStartCenterLon = orbitalMap.centerLon;
      dragStartCenterLat = orbitalMap.centerLat;
    } else if (e.touches.length === 2) {
      isDragging = false;
      const dx = e.touches[0].clientX - e.touches[1].clientX;
      const dy = e.touches[0].clientY - e.touches[1].clientY;
      touchStartDist = Math.sqrt(dx * dx + dy * dy);
      touchStartZoom = orbitalMap.zoom;
    }
    e.preventDefault();
  }, { passive: false });

  canvas.addEventListener('touchmove', (e) => {
    if (!orbitalMap) return;
    if (e.touches.length === 1 && isDragging) {
      const dx = e.touches[0].clientX - dragStartX;
      const dy = e.touches[0].clientY - dragStartY;
      const dLon = -(dx / canvas.width) * 360 / orbitalMap.zoom;
      const dLat = (dy / canvas.height) * 180 / orbitalMap.zoom;
      orbitalMap.centerLon = dragStartCenterLon + dLon;
      orbitalMap.centerLat = dragStartCenterLat + dLat;
      orbitalMap._baseDirty = true;
      orbitalMap.render();
    } else if (e.touches.length === 2 && touchStartDist) {
      const dx = e.touches[0].clientX - e.touches[1].clientX;
      const dy = e.touches[0].clientY - e.touches[1].clientY;
      const dist = Math.sqrt(dx * dx + dy * dy);
      const scale = dist / touchStartDist;
      orbitalMap.zoom = Math.max(1, Math.min(8, touchStartZoom * scale));
      orbitalMap._baseDirty = true;
      orbitalMap.render();
    }
    e.preventDefault();
  }, { passive: false });

  canvas.addEventListener('touchend', () => {
    isDragging = false;
    touchStartDist = null;
  });

  // Resize canvas to match container width
  orbitalMap.resize();
  // Initial render
  orbitalMap.render();
}

// Update orbital map with current satellite data
function updateOrbitalMap() {
  if (!orbitalMap) return;

  if (selectedSatIndex < 0 || selectedSatIndex >= satellites.length) {
    // No satellite selected — clear the map
    const canvas = document.getElementById('orbitalMap');
    if (canvas) {
      const ctx = canvas.getContext('2d');
      ctx.fillStyle = '#0c1929';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
    }
    return;
  }

  const sat = satellites[selectedSatIndex];
  try {
    const satrec = satrecFromSat(sat);
    const now = new Date();

    // Get current satellite ground position
    const currentPos = getSatGroundPosition(satrec, now);
    if (currentPos) {
      orbitalMap.setSatPos(currentPos.lat, currentPos.lon);
    }

    // Generate ground track
    const duration = parseInt(document.getElementById('mapTrackDuration').value) || 180;
    const track = generateGroundTrack(satrec, duration, 2);
    orbitalMap.setGroundTrack(null, track.future, track.orbitSegments);

    // Compute terminator
    const terminator = computeTerminator(now);
    orbitalMap.setTerminator(terminator, document.getElementById('mapShowDayNight').checked);

    // Update map footer
    const footer = document.getElementById('mapFooter');
    if (footer && currentPos) {
      const altKm = Math.max(0, currentPos.alt).toFixed(0);
      // Compute approximate velocity from orbital period
      const periodMin = (2 * Math.PI / satrec.no) / 60; // minutes
      const circumference = 2 * Math.PI * (6378 + parseFloat(altKm)); // km
      const velKms = (circumference / periodMin / 60).toFixed(1);

      // Great-circle distance to station
      const dLat = (currentPos.lat - stationLocation.lat) * Math.PI / 180;
      const dLon = (currentPos.lon - stationLocation.lon) * Math.PI / 180;
      const a = Math.sin(dLat/2)**2 + Math.cos(stationLocation.lat*Math.PI/180) * Math.cos(currentPos.lat*Math.PI/180) * Math.sin(dLon/2)**2;
      const distKm = (2 * 6371 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a))).toFixed(0);

      footer.innerHTML = `
        <span>POS: ${currentPos.lat.toFixed(1)}°${currentPos.lat >= 0 ? 'N' : 'S'} ${Math.abs(currentPos.lon).toFixed(1)}°${currentPos.lon >= 0 ? 'E' : 'W'}</span>
        <span>ALT: ${altKm} km</span>
        <span>VEL: ${velKms} km/s</span>
        <span>DIST: ${distKm} km</span>
      `;
    }

    orbitalMap.render();
  } catch (e) {
    console.warn('Orbital map update failed:', e);
  }
}

// Start/stop map update interval
function startMapUpdates() {
  if (mapUpdateInterval) clearInterval(mapUpdateInterval);
  mapUpdateInterval = setInterval(updateOrbitalMap, 30000); // Update every 30 seconds
  updateOrbitalMap(); // Initial update
}

function stopMapUpdates() {
  if (mapUpdateInterval) {
    clearInterval(mapUpdateInterval);
    mapUpdateInterval = null;
  }
}

// --- Hook into satellite selection ---
const origUpdateCurrentSatDisplay = window.updateCurrentSatDisplay;
window.updateCurrentSatDisplay = function () {
  if (origUpdateCurrentSatDisplay) origUpdateCurrentSatDisplay();
  // Update map when satellite changes
  stopMapUpdates();
  if (selectedSatIndex >= 0) {
    startMapUpdates();
  } else {
    updateOrbitalMap(); // Clear map
  }
};

// Re-wrap the satellite selection change handler
const trackSatSelect = document.getElementById('trackSatSelect');
const origOnChange = trackSatSelect.onchange;
trackSatSelect.addEventListener('change', () => {
  // Give the original handler time to update selectedSatIndex
  setTimeout(() => {
    stopMapUpdates();
    if (selectedSatIndex >= 0) {
      startMapUpdates();
    } else {
      updateOrbitalMap(); // Clear map
    }
  }, 50);
});

// Initialize map on page load
window.addEventListener('DOMContentLoaded', () => {
  initOrbitalMap();
  if (selectedSatIndex >= 0) {
    startMapUpdates();
  }
});
