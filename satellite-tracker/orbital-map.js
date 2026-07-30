// ============================================================
// Orbital Map — Canvas-based offline world map for AZELCtrl
// ============================================================
// Embeds world coastline data, renders satellite ground tracks
// on a Canvas with equirectangular projection. No external deps.
// ============================================================

// --- Orbital Map Renderer ---
class OrbitalMapRenderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.centerLat = 0;
    this.centerLon = 0;
    this.zoom = 1; // 1 = full world, 2 = 2x zoom, etc.
    this.rings = []; // simplified coastline rings
    this.groundTrack = null; // { past: [[lat, lon], ...], future: [[lat, lon], ...] }
    this.satPos = null; // [lat, lon]
    this.stationPos = null; // [lat, lon]
    this.terminator = null; // [[lat, lon], ...]
    this.showTerminator = false;
    this.showGrid = true;
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

  // Set coastline data (array of rings, each ring is [[lat, lon], ...])
  setCoastline(rings) {
    this.rings = rings;
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
  }

  // Render the full map
  render() {
    const w = this.canvas.width;
    const h = this.canvas.height;
    const ctx = this.ctx;

    // 1. Fill ocean background
    ctx.fillStyle = '#0c1929';
    ctx.fillRect(0, 0, w, h);

    // 2. Draw land masses
    ctx.fillStyle = '#1a2a1a';
    ctx.strokeStyle = '#2a4a2a';
    ctx.lineWidth = 0.5;
    for (const ring of this.rings) {
      this._drawRing(ring);
    }

    // 3. Draw day/night terminator
    if (this.showTerminator && this.terminator && this.terminator.length > 0) {
      this._drawTerminator();
    }

    // 4. Draw ground track
    if (this.groundTrack) {
      const w = this._logicalWidth || 800;

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
    this._drawGrid();

    // 6. Draw markers
    if (this.stationPos) {
      this._drawMarker(this.stationPos[0], this.stationPos[1], '#22c55e', 'home', 6);
    }
    if (this.satPos) {
      this._drawMarker(this.satPos[0], this.satPos[1], '#38bdf8', 'diamond', 8);
    }
  }

  _drawRing(ring) {
    const ctx = this.ctx;
    if (ring.length < 3) return;

    const w = this._logicalWidth || 800;
    const h = this._logicalHeight || 280;

    ctx.beginPath();
    let started = false;
    for (let i = 0; i < ring.length; i++) {
      const [x, y] = this.project(ring[i][0], ring[i][1]);
      if (!started) {
        ctx.moveTo(x, y);
        started = true;
      } else {
        // Detect world-edge wrap: if consecutive points jump across the map,
        // break the path to avoid cross-map lines
        const prevX = this.project(ring[i-1][0], ring[i-1][1])[0];
        if (Math.abs(x - prevX) > w * 0.8) {
          ctx.moveTo(x, y);  // wrap break — new subpath
        } else {
          ctx.lineTo(x, y);
        }
      }
    }
    ctx.fill();
    ctx.stroke();
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

  // Altitude (for display)
  const N = a / Math.sqrt(1 - e2 * Math.sin(lat) * Math.sin(lat));
  const alt = (rho - N) * Math.cos(lat) + (z - N * (1 - e2) * Math.sin(lat)) * Math.sin(lat);

  return {
    lat: lat * 180 / Math.PI,
    lon: lon * 180 / Math.PI,
    alt: alt // km above WGS84 ellipsoid
  };
}

// Orbit track colors — 5 distinct colors visible against dark background
const ORBIT_COLORS = ['#ef4444', '#38bdf8', '#fbbf24', '#a78bfa', '#34d399'];

// Generate ground track points for a satellite (future only), split into orbit segments
function generateGroundTrack(satrec, durationMinutes = 180, stepMinutes = 2) {
  const now = new Date();
  const futurePoints = [];
  const orbitSegments = []; // [{orbitNum, points: [[lat, lon], ...]}, ...]

  // Compute orbit number from mean anomaly at epoch
  // satrec.no = mean motion (rad/s), satrec.mo = mean anomaly at epoch (rad)
  const epochTime = new Date(satrec.jdsatepoch * 86400 * 1000); // approximate
  const epochMs = epochTime.getTime();
  const secondsPerOrbit = (2 * Math.PI) / satrec.no;
  
  // Compute orbit number at 'now'
  const elapsedSec = (now.getTime() - epochMs) / 1000;
  const meanMotionAtNow = satrec.mo + satrec.no * elapsedSec;
  const orbitNumNow = Math.floor(meanMotionAtNow / (2 * Math.PI));

  // Future track
  let currentOrbit = orbitNumNow;
  let currentSegment = [];
  for (let i = stepMinutes; i <= durationMinutes; i += stepMinutes) {
    const time = new Date(now.getTime() + i * 60 * 1000);
    const pos = getSatGroundPosition(satrec, time);
    if (pos) {
      futurePoints.push([pos.lat, pos.lon]);
      // Compute orbit number at this time
      const elapsed = (time.getTime() - epochMs) / 1000;
      const meanAnomaly = satrec.mo + satrec.no * elapsed;
      const orbitNum = Math.floor(meanAnomaly / (2 * Math.PI));
      if (orbitNum !== currentOrbit) {
        if (currentSegment.length > 1) {
          orbitSegments.push({ orbitNum: currentOrbit, points: currentSegment });
        }
        currentOrbit = orbitNum;
        currentSegment = [];
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

  // Load coastline data from SPIFFS
  fetch('/world_coastline.js')
    .then(r => {
      if (!r.ok) throw new Error('world_coastline.js not found on SPIFFS');
      return r.text();
    })
    .then(text => {
      // Execute the coastline data file to define WORLD_OUTLINES
      const script = document.createElement('script');
      script.textContent = text;
      document.head.appendChild(script);
      // Now set the coastline
      if (typeof WORLD_OUTLINES !== 'undefined') {
        orbitalMap.setCoastline(WORLD_OUTLINES);
        orbitalMap.setStationPos(stationLocation.lat, stationLocation.lon);
        orbitalMap.centerOn(stationLocation.lat, stationLocation.lon);
        orbitalMap.render();
      }
    })
    .catch(e => {
      console.warn('Orbital map: coastline data not available:', e.message);
      // Show message on the map
      const container = document.getElementById('orbitalMapContainer');
      if (container) {
        const msg = document.createElement('div');
        msg.className = 'banner warn';
        msg.style.cssText = 'margin:12px;padding:16px;text-align:center;';
        msg.innerHTML = `
          <strong>World map data not found</strong><br>
          <span style="font-size:0.8rem;">The file <code>world_coastline.js</code> is missing from the ESP filesystem.<br>
          Upload it via the <strong>Manage</strong> tab to enable the orbital map.</span>
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
      orbitalMap.render();
    } else if (e.touches.length === 2 && touchStartDist) {
      const dx = e.touches[0].clientX - e.touches[1].clientX;
      const dy = e.touches[0].clientY - e.touches[1].clientY;
      const dist = Math.sqrt(dx * dx + dy * dy);
      const scale = dist / touchStartDist;
      orbitalMap.zoom = Math.max(1, Math.min(8, touchStartZoom * scale));
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
    orbitalMap.setTerminator(terminator, true);

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
