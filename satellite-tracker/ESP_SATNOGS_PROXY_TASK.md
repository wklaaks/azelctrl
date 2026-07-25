# ESP SatNOGS Proxy Endpoint — Implementation Task

**Project:** AZELCtrl Satellite Tracker  
**Firmware file:** `ycint/ycint.ino`  
**Web UI file:** `satellite-tracker/index.html`  
**Version:** 1.3.0+  
**Date:** 2026-07-25  
**Status:** Planned (not yet implemented in firmware)

---

## Objective

Add a `/satnogs-proxy` endpoint to the ESP8266 web server to proxy HTTP requests to the SatNOGS DB API. This is required because the SatNOGS API does not support CORS, so browser-based fetches from the satellite tracker page are blocked.

When the tracker is served from the ESP device itself (the normal deployment mode), the proxy runs on the same origin and bypasses CORS restrictions entirely.

---

## Background

The satellite tracker (v1.3.0) integrates SatNOGS DB data to show:
- Satellite operational status (active / no signals / re-entered)
- Active transmitter frequencies (uplink, downlink) and modulation modes
- Filtering to exclude inactive satellites from pass predictions

The browser JavaScript makes requests like:
```
/satnogs-proxy?url=/satellites/?norad_cat_id=25544
/satnogs-proxy?url=/transmitters/?satellite__norad_cat_id=25544&alive=true&status=active
```

The ESP must forward these to:
```
https://db.satnogs.org/api/satellites/?norad_cat_id=25544
https://db.satnogs.org/api/transmitters/?satellite__norad_cat_id=25544&alive=true&status=active
```

---

## Implementation Details

### 1. Required Includes

Add at the top of `ycint.ino` (with other includes):

```cpp
#include <ESP8266HTTPClient.h>
```

### 2. New Endpoint Handler

Add a new handler function and route registration in the `startWebServer()` function (around line 5398+ where other routes are defined).

**Route registration** (add with other `server.on()` calls):

```cpp
server.on("/satnogs-proxy", HTTP_GET, handleSatNOGSProxy);
```

**Handler function** (add before `startWebServer()`):

```cpp
void handleSatNOGSProxy() {
  // Extract the "url" query parameter
  String urlParam = server.arg("url");
  
  if (urlParam.isEmpty()) {
    server.send(400, "application/json", "{\"error\":\"Missing 'url' parameter\"}");
    return;
  }
  
  // Build the full SatNOGS API URL
  String targetUrl = "https://db.satnogs.org/api" + urlParam;
  
  // Make the HTTP request to SatNOGS
  HTTPClient http;
  http.begin(targetUrl);
  http.setTimeout(5000);  // 5 second timeout
  
  int httpCode = http.GET();
  
  if (httpCode <= 0) {
    String error = http.errorToString(httpCode);
    http.end();
    server.send(502, "application/json", "{\"error\":\"Request failed: \" + error}");
    return;
  }
  
  // Read the response body
  String body = http.getString();
  http.end();
  
  // Forward the response to the browser with CORS headers
  server.send(httpCode, "application/json", body);
}
```

### 3. Memory Considerations

- `ESP8266HTTPClient` is already part of the ESP8266 core — no extra library needed
- The proxy uses a synchronous request (blocking for up to 5 seconds)
- This is acceptable because:
  - SatNOGS requests are infrequent (only on satellite add, manual refresh, or auto-refresh on load)
  - The ESP web server is single-threaded anyway
  - A 5-second timeout prevents hangs
- Estimated memory impact: ~2KB RAM during active request

### 4. Error Handling

The handler should:
- Return 400 if `url` parameter is missing
- Return 502 if the HTTP request to SatNOGS fails (timeout, connection error)
- Return the SatNOGS API's own HTTP status code for valid responses (200, 404, etc.)
- Always set `Content-Type: application/json`

### 5. Testing

After flashing the updated firmware:

1. Open the satellite tracker page (from the ESP device)
2. Add a satellite via CelesTrak
3. Check browser console — SatNOGS fetches should succeed (no CORS errors)
4. Go to Manage → SatNOGS Data tab
5. Click "Refresh SatNOGS Data"
6. Verify:
   - Progress indicator updates
   - "Last refresh" shows a timestamp (not "Never")
   - Status shows "Data cached for X/Y satellites"
7. Go to Satellites tab
8. Verify:
   - Status dots appear (green/yellow/red)
   - DL Freq, UL Freq, and Mode columns show data
   - Multi-transmitter satellites show multiple rows

---

## Integration with Local Dev Server

During local development, the Python dev server (`server.py`) already provides the same `/satnogs-proxy` endpoint. When deploying to the ESP, the firmware's endpoint replaces the Python one.

The JavaScript code in `index.html` uses a relative URL (`/satnogs-proxy?url=...`), so it works identically whether served from:
- `http://localhost:8080` (local dev with Python server)
- `http://192.168.4.1` (ESP AP mode)
- `http://192.168.1.55` (ESP STA mode on home WiFi)

---

## Notes

- The SatNOGS API returns JSON arrays directly (not wrapped in `{results: [...]}`)
- The browser JavaScript already handles this correctly
- No changes needed to `index.html` for this firmware update
- The proxy should be added after the existing endpoints (`/getpos`, `/setpos`, `/elf`, etc.) in `startWebServer()`
