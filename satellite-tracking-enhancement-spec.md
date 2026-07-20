# Satellite Tracking Enhancement — Specification Document

## Purpose of this document
This document describes an enhancement to an existing ESP8266-based Yaesu Az/El rotator controller. It is intended as a brief for an AI coding agent (or human developer) continuing work on the existing codebase. It covers the goal, architecture, recommended libraries given the device's resource constraints, the feature set, the UI layout, and the storage strategy.

---

## 1. Background: what already exists

The existing product is an ESP8266-based controller box that interfaces with a Yaesu Az/El rotator controller (reading position via an ADS1115 ADC from the Yaesu's potentiometer outputs, driving rotation via existing logic). It already supports:

- Control from a PC over USB serial using GS-232A, GS-232B, and EasyComm protocols
- Control from PC software via the Hamlib standard
- An OLED (SSD1306, 128×64) status display
- WiFi STA/AP modes with a captive portal (DNSServer + ESP8266WebServer)
- Settings/calibration/WiFi credentials stored in flash-emulated EEPROM

**Current resource usage (baseline, before this enhancement):**

| Resource | Used | Total | Free | Usage |
|---|---|---|---|---|
| Flash (code) | 334 KB | 1 MB | ~714 KB | 31% |
| RAM (data) | 37.6 KB | 80 KB | ~42.5 KB | 46% |
| IRAM | 61 KB | 65.5 KB | ~4.5 KB | **93%** |

**IRAM is the binding constraint**, not flash or RAM. IRAM on ESP8266 is a small, fixed pool used for time-critical code (ISRs, WiFi stack internals, some core functions). Unlike flash or heap RAM, there is no way to "make more space" by being economical elsewhere — if IRAM overflows, the firmware will not link, regardless of how much flash or heap headroom exists. This constraint drives several of the library recommendations below.

---

## 2. Goal of this enhancement

Add a basic satellite tracking capability to the device, without requiring any orbital mechanics or Keplerian element storage on the ESP8266 itself. The design principle: **the ESP8266 does zero satellite math and stores zero satellite data.** It only:

1. Serves a small web page (HTML/CSS/JS) to a browser on the same WiFi network
2. Receives target Azimuth/Elevation values from that browser
3. Feeds those values into the rotator control logic that already exists (the same internal function currently driven by GS-232/EasyComm commands)
4. Reports the current actual Az/El back to the browser on request

All satellite-specific logic — TLE storage, SGP4 orbital propagation, look-angle (Az/El) computation, pass prediction, and Keplerian element refresh — runs **in the connecting browser**, using the JavaScript library `satellite.js`. TLEs and the user's location are cached in the browser's `localStorage`, not on the device.

### Why this split makes sense
- The ESP8266 has no room (IRAM/RAM) for orbital mechanics code, and could never store more than a handful of TLE sets in flash-emulated EEPROM anyway.
- A modern browser has effectively unlimited storage (megabytes of localStorage vs a few KB of EEPROM) and far more CPU headroom for the trigonometry involved in SGP4 propagation.
- The PC/browser and the ESP8266 are already on the same WiFi AP, and (per this use case) both have internet access — so the browser can fetch fresh TLEs directly from an online source like CelesTrak without the ESP8266 needing to proxy that traffic at all.

---

## 3. Recommended architecture and libraries (ESP8266 side)

### Do NOT introduce ESPAsyncWebServer / ESPAsyncTCP
Given IRAM is already at 93%, avoid the Async web/WebSocket stack. It tends to pull in more IRAM-resident code (interrupt-driven TCP handling) than the classic synchronous server already in use, and risks pushing the build over the IRAM limit entirely.

### Recommended approach: extend the existing synchronous `ESP8266WebServer` — no new libraries
Rather than adding a WebSocket library (even a lighter synchronous one), the simplest and lowest-risk path is to add two additional endpoint handlers to the **existing** `ESP8266WebServer` instance already running for the captive portal / web UI:

```cpp
server.on("/setpos", HTTP_POST, handleSetPos);   // browser -> ESP: new target az/el
server.on("/getpos", HTTP_GET, handleGetPos);    // browser <- ESP: current az/el
server.on("/", HTTP_GET, handleTrackerPage);      // serves the tracker HTML/JS page
```

- `handleSetPos`: parses a small JSON body (or simple query-string params) containing target `az`/`el`, then calls straight into whatever internal function is currently invoked when a GS-232 `W` (or equivalent) command is received. No protocol translation needed — the browser sends floats directly.
- `handleGetPos`: reads the current Az/El from existing internal state (the same values already read from the ADS1115/potentiometer) and returns them as a small JSON response.
- The browser's own JavaScript timer (already ticking every 1–5 seconds to recompute satellite position via `satellite.js`) drives both requests via `fetch()`. No persistent connection is required — satellite tracking only needs ~1 Hz updates, and LAN-local HTTP request/response overhead at that rate is negligible.

This adds **zero new libraries** and reuses code paths already implemented and tested. It should be validated early: add the two handlers with stub (empty JSON) bodies first, recompile, and confirm IRAM usage barely moves before writing the actual tracking logic.

**If a persistent connection is later found genuinely necessary** (e.g. for a live-updating polar plot with sub-second refresh), the fallback option is `WebSocketsServer` from the `arduinoWebSockets` (Links2004) library, which is lighter than the Async stack and can run alongside `ESP8266WebServer`. This should only be adopted if Option A (plain HTTP) proves insufficient, and IRAM usage must be re-checked immediately after adding it — abandon it if it pushes usage uncomfortably close to the ceiling.

### Filesystem for serving the web page
Use **LittleFS** (not SPIFFS) to store the tracker's HTML/CSS/JS bundle. LittleFS is more robust and is the current recommended default in the ESP8266 Arduino core. A minified `satellite.js` bundle plus a small UI page (well under 100 KB total) fits comfortably within the available flash — this is not a constrained resource in this project, unlike IRAM.

### EEPROM usage — no change needed
The existing EEPROM library (flash-emulated on ESP8266) continues to be used only for WiFi credentials, calibration, AP password, and settings, exactly as today. **No satellite-related data (TLEs, location) should be stored here.** This isn't a matter of squeezing data in — flash-emulated EEPROM on ESP8266 could technically hold a few KB if `EEPROM.begin()` were sized up — but storing TLEs there would still be the wrong architectural choice, since TLEs need to be refreshed periodically from the internet and are naturally a browser-side concern given the browser already has the connectivity and the storage capacity to do this properly.

### JSON payload — keep it minimal
To avoid heap fragmentation risk (a bigger practical concern on ESP8266 than ESP32 given its smaller heap), use `ArduinoJson` with small fixed-capacity documents (e.g. `StaticJsonDocument<192>`) rather than dynamic allocation. Suggested schema:

```json
// Browser -> ESP8266 (POST /setpos)
{"az": 182.4, "el": 45.1}

// ESP8266 -> Browser (GET /getpos)
{"az": 181.9, "el": 44.8, "maz": "cw", "mel": "up"}
```

### Movement direction indicators (`maz` / `mel`)
In addition to the current Az/El readout, the `/getpos` response should include the rotator's **current movement direction** on each axis, so the browser UI can visually indicate whether the rotator is actively slewing and in which direction — useful feedback for the user watching the tracking process, distinct from simply comparing target vs. current position (which can be ambiguous near overshoot/settling).

Use a small fixed vocabulary of short string values rather than a numeric enum. At this payload size (well within a small fixed-capacity JSON buffer) the cost difference between strings and integers is negligible, and strings are self-documenting when debugging the API directly (e.g. via a browser console or `curl`), and require no lookup table on the browser side:

- `maz`: `"cw"` (clockwise) | `"ccw"` (counter-clockwise) | `"idle"` (not moving)
- `mel`: `"up"` | `"down"` | `"idle"`

Examples:
```json
{"az": 181.9, "el": 44.8, "maz": "cw",   "mel": "up"}
{"az": 181.9, "el": 44.8, "maz": "ccw",  "mel": "down"}
{"az": 181.9, "el": 44.8, "maz": "cw",   "mel": "idle"}
{"az": 181.9, "el": 44.8, "maz": "idle", "mel": "idle"}
```

This value set should be treated as fixed and documented once, exactly like a formal enum, even though it is serialized as short strings — no other values should ever be emitted, and the browser-side JS should treat unrecognized values defensively (e.g. fall back to an "idle"-style display) in case firmware and UI versions drift apart during development.

The ESP8266 side should already have (or trivially expose) whatever internal state currently drives the rotator's motor outputs, so populating `maz`/`mel` should just mean reading that existing state — no new movement-detection logic needs to be invented for this purpose.

---

## 4. Recommended feature set

Based on review of comparable open-source satellite tracking / rotator projects (e.g. AntRunner, satvisor, ok5tvr/satelite_tracker), the following feature set is achievable within this architecture, roughly ordered from essential to optional/stretch:

### Core (must-have)
1. **TLE input and storage** — paste in raw TLE (two-line element) text for one or more satellites; store in browser `localStorage`.
2. **TLE freshness tracking** — record the timestamp each TLE was saved/fetched; show TLE age in the UI; warn when a TLE is older than a configurable threshold (e.g. 24–72 hours, shorter for fast-decaying LEO objects).
3. **Automatic TLE fetch from CelesTrak** — since both the browser and ESP8266 have internet access, the browser can fetch current TLEs directly from CelesTrak (no need to route through the ESP8266) for a user-selected list of satellites or NORAD IDs.
4. **Ground station location input** — via either direct latitude/longitude entry, or a **Maidenhead grid square locator** (e.g. `KG44BD`) that is converted to lat/lon in-browser. Store in `localStorage`.
5. **Real-time Az/El computation** — using `satellite.js` (SGP4/SDP4 propagation) at ~1 Hz, computing topocentric Az/El/range for the selected satellite based on current time and stored ground station location.
6. **Send target Az/El to rotator** — on each computation tick, POST the new target to `/setpos` on the ESP8266.
7. **Display current rotator position** — poll `/getpos` and show the live actual Az/El alongside the computed target Az/El (useful to visually confirm the rotator is keeping up).
8. **Manual/park control** — a manual Az/El entry or "park" button that bypasses tracking, sent the same way via `/setpos`.

### Recommended (high value, still lightweight)
9. **Pass prediction list** — compute and list upcoming passes (AOS/LOS time, max elevation, duration) for the selected satellite over some lookahead window (e.g. next 24–48 hours). This is pure client-side computation using `satellite.js`, no extra device load.
10. **Multi-satellite selection** — store several TLE sets and let the user pick which one is actively tracked, plus a simple built-in shortlist of common amateur satellites (ISS, SO-50, AO-91, etc.) to reduce data entry.
11. **Polar plot / simple pass visualization** — a small SVG or canvas polar plot showing the satellite's track across the sky and the rotator's current position as a dot, similar in spirit to features seen in AntRunner and satvisor.
12. **Doppler shift display** (informational only) — since `satellite.js` gives range-rate, the frequency Doppler correction can be computed and displayed for reference even though the ESP8266 doesn't control radio frequency in this project.
13. **Start/stop tracking toggle** — explicit control over whether the tracking loop is actively sending position updates, separate from simply having a satellite selected.

### Stretch / optional
14. **Look4Sat / Gpredict-style external control compatibility** — not required given Hamlib/GS-232/EasyComm are already supported over serial/USB for PC software; browser tracking is an additional, independent control path, not a replacement.
15. **Cable wrap / limit awareness in the UI** — if the existing rotator logic already enforces azimuth limits or cable-wrap handling internally, surface a simple visual indicator in the browser UI (this logic itself stays entirely on the ESP8266 side, unchanged).
16. **Offline/cached fallback TLE** — bundle one or two built-in fallback TLEs (e.g. for ISS) directly in the web page JS as a last resort if no TLE has been fetched/entered yet.

---

## 5. User interface layout

Given the constrained device (small OLED already used for basic status) and the browser being the primary interface for this feature, the tracking UI should live entirely in the browser page served from the ESP8266, structured as a small number of simple views/tabs rather than a complex single page. Suggested structure — a single-page app with tab-style navigation (no server-side page loads needed after the initial fetch):

### Tab 1 — Tracking (default/home view)
- Currently selected satellite name and TLE age (with a warning indicator if stale)
- Large readout of **Target Az/El** (computed) and **Current Az/El** (actual, from rotator)
- **Movement direction indicators** (e.g. small arrows or a rotating icon) for azimuth and elevation, reflecting the `maz`/`mel` values from `/getpos` — showing at a glance whether the rotator is actively slewing cw/ccw/up/down or idle
- A simple polar plot showing satellite track and current rotator position
- Start/Stop tracking button
- Manual Az/El entry fields + "Set" button (for manual slewing, independent of tracking)
- Countdown to next AOS if no satellite is currently above the horizon, or elapsed/remaining time if a pass is in progress

### Tab 2 — Satellites / TLE management
- List of stored satellites with TLE age per entry
- "Fetch latest from CelesTrak" button (per-satellite or bulk, since internet access is available)
- Manual TLE paste box (two-line element text entry) for satellites not in CelesTrak or for offline-provided data
- Delete/remove entries
- Optional: a small built-in shortlist of common amateur satellites to add with one click (avoids the user having to know NORAD IDs)

### Tab 3 — Pass predictions
- Table of upcoming passes for the currently selected satellite: AOS time, max elevation, LOS time, duration
- Tap/click a pass to jump to it in the polar plot / preview the track
- Lookahead window selector (e.g. 24h / 48h / 72h)

### Tab 4 — Location / Settings
- Latitude/longitude direct entry
- **Grid square (Maidenhead locator) entry field** — e.g. `KG44BD` — with automatic conversion to lat/lon shown alongside for confirmation, and a small explanatory note ("6-character grid square gives ~5×5 km resolution")
- Elevation/altitude of the station (optional, minor accuracy improvement for SGP4)
- Tracking update interval setting (e.g. 1s / 2s / 5s) — trades responsiveness against network/CPU load on both ends
- Minimum elevation threshold used for pass prediction filtering (e.g. ignore passes under 10°)

This tab structure keeps each screen focused on one concern, avoids clutter, and maps cleanly onto a mobile-friendly single-page layout, since amateur operators may well load this page on a phone while standing next to the antenna.

---

## 6. Storage strategy summary

| Data | Where stored | Why |
|---|---|---|
| WiFi credentials, calibration, AP password, device settings | ESP8266 flash-emulated EEPROM (unchanged) | Small, static, device-specific — no change from current design |
| TLE sets (one or more satellites) | Browser `localStorage` | Needs to hold potentially many sets, refreshed periodically; browser has ample storage and internet access to fetch updates directly |
| TLE fetch timestamp (per satellite) | Browser `localStorage` | Drives the "staleness" warning in the UI |
| Ground station lat/lon (or grid square) | Browser `localStorage` | User-specific, browser is the natural place given it's entered once and reused across sessions |
| Tracking/UI preferences (update interval, elevation threshold, selected satellite) | Browser `localStorage` | Convenience only, no need to burden the ESP8266 |
| HTML/CSS/JS for the tracking UI, including the `satellite.js` bundle | ESP8266 LittleFS (flash) | Served once per page load; flash has ample headroom (31% used) even after adding this |
| Live target/current Az/El values | Not persisted anywhere — transient, exchanged live via `/setpos` and `/getpos` | No need to persist; recomputed or re-read every cycle |

**Net effect on the ESP8266:** the only genuinely new persistent storage requirement is the LittleFS-hosted web page bundle (a few tens of KB of flash, not IRAM or RAM), plus two new lightweight HTTP handlers reusing existing internal control functions. No new EEPROM usage, no orbital data, no TLEs, and — per the recommended approach — no new networking libraries beyond what's already linked into the firmware.

---

## 7. Standalone development workflow (recommended, before final integration)

Rather than iterating on the tracking UI by repeatedly compiling and re-flashing the ESP8266 firmware, the HTML/CSS/JS should be developed and tested as a **standalone web page**, run from any ordinary local web server on a PC (e.g. a simple static file server, or even opened directly as a `file://` page during early work), while still making its live `/setpos` and `/getpos` calls to the real ESP8266 device over the WiFi network. This is possible because the page only needs to know the device's IP address to talk to it — it does not need to be served *by* the device to *call* the device.

### Why this works
The browser doesn't care where the HTML/JS came from in order to make an HTTP request elsewhere — a page loaded from a laptop's local dev server can still `fetch()` a request to `http://<esp8266-ip>/getpos` on the same LAN, exactly as it would if the ESP8266 had served the page itself. The only two things that need to be handled deliberately are:

1. **The base URL must be configurable**, since it changes between "talking to the real ESP8266 IP during development" and "being served by the device itself in production."
2. **Cross-Origin Resource Sharing (CORS)**, since the page's origin (e.g. `http://localhost:8080` or `file://`) will differ from the ESP8266's origin (`http://<device-ip>`). Browsers block cross-origin requests by default unless the server explicitly allows them.

### Instruction for the implementing agent

> **Add a configurable API base URL to the tracking web page's JavaScript.**
> Do not hardcode `/setpos` and `/getpos` as relative paths. Instead, define a single `apiBase` variable/constant (defaulting to an empty string `""`, which correctly resolves to relative paths when the page *is* served by the device itself in production). When the page is opened for standalone development/testing, allow the user to enter the device's IP address once (e.g. via a small input field, a URL query parameter such as `?device=192.168.1.55`, or a value saved in `localStorage` alongside the other tracking preferences already specified in this document). Build all API calls from this base:
> ```js
> const apiBase = localStorage.getItem('deviceApiBase') || '';
> fetch(`${apiBase}/getpos`);
> fetch(`${apiBase}/setpos`, { method: 'POST', body: JSON.stringify({...}) });
> ```
> This single change means the exact same HTML/CSS/JS files work unmodified whether they are opened from a developer's PC pointed at the device's IP, or uploaded to the device's LittleFS and served by the ESP8266 itself — no code fork or build step is needed to switch between the two.
>
> **Add CORS headers to the ESP8266's `/setpos` and `/getpos` handlers** (and any other API-style endpoints added for this feature), so that requests from a page hosted elsewhere on the network are not blocked by the browser. On `ESP8266WebServer`, this means adding an `Access-Control-Allow-Origin` header (a permissive `*` is acceptable here, since this is a LAN-only device with no sensitive data exposed by these two endpoints) to every response from these handlers:
> ```cpp
> server.sendHeader("Access-Control-Allow-Origin", "*");
> ```
> If `POST` requests with a JSON body are used (as recommended for `/setpos`), the browser will first send a **CORS preflight** `OPTIONS` request before the actual `POST`. Add a handler for `OPTIONS` on the same path that simply responds with the appropriate CORS headers and an empty response:
> ```cpp
> server.on("/setpos", HTTP_OPTIONS, []() {
>   server.sendHeader("Access-Control-Allow-Origin", "*");
>   server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
>   server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
>   server.send(204);
> });
> ```
> This CORS handling costs negligible flash/RAM and does not touch IRAM-sensitive networking internals, so it does not conflict with the resource constraints described elsewhere in this document. It can also simply be **left in place permanently**, even in production once the page is served from the device itself — same-origin requests are unaffected by CORS headers being present, so there is no need to strip this out later or maintain two versions of the firmware.

### Practical development flow this enables
1. Build and preview the tracking page entirely on a PC (any static file server, or a bundler's live-reload dev server), pointed at the real device's IP address for live `/setpos`/`/getpos` calls.
2. Iterate on layout, the polar plot, pass prediction tables, and `satellite.js` integration with instant browser refresh — no firmware compile/upload cycle required for any of this.
3. Once the UI is stable, copy the finished HTML/CSS/JS files into the LittleFS data folder for upload to the device as the final step, at which point `apiBase` naturally resolves to relative paths and everything continues to work identically, served directly from the ESP8266.

---

## 8. Summary of key decisions for the implementing agent

1. **All orbital math (SGP4/SDP4) runs in the browser via `satellite.js`.** The ESP8266 never sees a TLE.
2. **Communication is plain HTTP** via two new handlers (`/setpos`, `/getpos`) on the existing `ESP8266WebServer` — no WebSocket library added unless later proven necessary, to protect the already-tight IRAM budget.
3. **Use LittleFS**, not SPIFFS, to host the web UI bundle.
4. **TLEs, ground station location, and UI preferences all live in browser `localStorage`**, not EEPROM.
5. **Ground station location supports both direct lat/lon entry and Maidenhead grid square entry** (e.g. `KG44BD`), converted client-side.
6. **Both the browser and the ESP8266 have internet access**, so TLE fetching from CelesTrak happens directly from the browser — the ESP8266 does not need to proxy this traffic.
7. Validate the IRAM budget immediately after adding the two new stub handlers, before implementing any tracking logic, to confirm the approach is safe before further investment.
8. **Develop and test the tracking web page standalone**, on a PC, against the real device's IP over the LAN, using a configurable `apiBase` and CORS headers on the ESP8266 endpoints (see Section 7). Only copy the finished files onto the device's LittleFS as the final step.
9. **`/getpos` includes movement direction** (`maz`: `cw`/`ccw`/`idle`, `mel`: `up`/`down`/`idle`) alongside the Az/El readout, using a fixed short-string vocabulary rather than a numeric enum, so the browser UI can show live movement feedback with no lookup table needed.
