# SatNOGS DB Integration — Implementation Plan

**Project:** AZELCtrl Satellite Tracker  
**File:** `satellite-tracker/index.html`  
**Version:** 1.2.19+  
**Date:** 2026-07-24  
**Status:** Planned (not yet implemented)

---

## 1. Objective

Integrate the SatNOGS DB API to provide amateur radio satellite operators with:
- Satellite operational status (alive / re-entered / no active transmitters)
- Active transmitter frequencies (uplink, downlink) and modulation modes
- Filtering to exclude inactive/dead satellites from pass predictions
- Automatic and manual refresh of SatNOGS data

This eliminates the need for users to manually look up or enter frequency/mode information for each satellite.

---

## 2. Data Source

**SatNOGS DB API:** https://db.satnogs.org/api/

### Endpoints Used

| Endpoint | Purpose | Query Params |
|----------|---------|--------------|
| `/api/satellites/?norad_cat_id=X` | Satellite status info | `norad_cat_id` |
| `/api/transmitters/?satellite__norad_cat_id=X&alive=true&status=active` | Active transmitters | `satellite__norad_cat_id`, `alive`, `status` |
| `/api/modes/` | Mode reference (optional) | — |

### Key Fields from `/api/satellites/`

- `norad_cat_id`: NORAD catalog number
- `name`: Official name
- `names`: Alternate names (comma-separated)
- `status`: "alive" | "re-entered" | "decommissioned"
- `decayed`: null or ISO date of re-entry
- `is_frequency_violator`: boolean

### Key Fields from `/api/transmitters/`

- `norad_cat_id`: NORAD catalog number
- `uplink_low`, `uplink_high`: Uplink frequency range in Hz (null if not applicable)
- `downlink_low`, `downlink_high`: Downlink frequency range in Hz (null if not applicable)
- `mode`: Modulation mode (FM, USB, LSB, BPSK, CW, SSTV, LoRa, etc.)
- `alive`: boolean
- `status`: "active" | "inactive"
- `baud`: Baud rate for digital modes

---

## 3. Local Data Model

### New localStorage Keys

| Key | Type | Description |
|-----|------|-------------|
| `satNOGSData` | Object | Cached SatNOGS data keyed by NORAD ID |
| `satNOGSRefreshInterval` | Number | Auto-refresh interval in days (default: 7) |

### Structure of `satNOGSData[norad_id]`

```javascript
{
  "24278": {
    "satellite": {
      "status": "alive",
      "decayed": null,
      "name": "JAS-2",
      "names": "FO-29"
    },
    "transmitters": [
      {
        "uplink_low": null,
        "uplink_high": null,
        "downlink_low": 435795000,
        "downlink_high": null,
        "mode": "CW",
        "alive": true,
        "status": "active"
      },
      {
        "uplink_low": 145900000,
        "uplink_high": 146000000,
        "downlink_low": 435800000,
        "downlink_high": null,
        "mode": "USB",
        "alive": true,
        "status": "active"
      }
    ],
    "lastUpdated": 1784900000000,
    "hasActiveTransmitters": true
  }
}
```

### Satellite Activity Classification

| Classification | Criteria | UI Indicator |
|----------------|----------|--------------|
| **Active** | status="alive" AND has active transmitters | Green dot ● |
| **No signals** | status="alive" AND no active transmitters | Yellow dot ● |
| **Re-entered** | status="re-entered" OR decayed date set | Red dot ● |

---

## 4. UI Changes

### 4.1 Satellites Tab — Table Enhancements

#### New Columns

Add the following columns to the satellite table (in this order):

| Column | Width | Content |
|--------|-------|---------|
| Status | 40px | ● Green/Yellow/Red dot |
| DL Freq | 120px | Downlink frequency or range |
| UL Freq | 120px | Uplink frequency or range |
| Mode | 80px | Modulation mode |

#### Multi-Row Display for Multiple Transmitters

- If a satellite has multiple active transmitters, display **one row per transmitter**
- Group visually: first row shows satellite name, subsequent rows for the same satellite show empty name cell but same AOS/LOS data
- Each row shows the frequency/mode for that specific transmitter
- Example:
  ```
  FO-29        ● 435.795    —        CW     AOS in: 12:34:56
               ● 435.800    145.900  USB    AOS in: 12:34:56
               ● 435.910    —        FM     AOS in: 12:34:56
  ```

#### Frequency Display Format

| Scenario | Display Format | Example |
|----------|---------------|---------|
| Single downlink | MHz with 3 decimals | `435.800` |
| Downlink range | Low-High | `435.800-435.910` |
| Single uplink | MHz with 3 decimals | `145.900` |
| Uplink range (SSB transponder) | Low-High | `145.800-145.950` |
| No uplink | Dash | `—` |

#### Active Satellites Filter Toggle

- Location: Below the "Satellites" tab header, above the table
- Label: "Show only active satellites"
- Type: Toggle switch or checkbox
- Behavior:
  - When ON: filter out satellites with status="re-entered" or no active transmitters
  - When OFF: show all satellites in user's list
  - Persists in localStorage (`satNOGSFilterActive`)

### 4.2 Tracking Tab — Frequency/Mode Display

#### Location

Below the satellite name in the SATELLITE box, above the AOS/LOS timer.

#### Content

Show the **primary transmitter** info (first active transmitter, or most common mode for that satellite):

```
FO-29
DL: 435.800 MHz USB | UL: 145.900-146.000 MHz

AOS in: 12:34:56
...
```

#### Multi-Transmitter Handling

- If satellite has multiple active transmitters, show the first one by default
- Add a small "(+2 more)" indicator that, when clicked, expands to show all active transmitters in a compact list
- Example expanded:
  ```
  FO-29
  DL: 435.800 MHz USB | UL: 145.900-146.000 MHz
    • 435.795 CW (beacon)
    • 435.910 FM
  ```

### 4.3 Manage Tab — Refresh Controls

#### New Section: "SatNOGS Data"

Add a new subsection in the Manage tab:

```
─────────────────────────────────────
SatNOGS Data
─────────────────────────────────────

Auto-refresh interval: [7] days  (dropdown or input)

Last refresh: 2026-07-24 14:30 UTC

[Refresh SatNOGS Data]  ← button

Status: Data current for 42/45 satellites
```

#### Refresh Behavior

1. **On click:**
   - Count how many satellites need refreshing (cached data older than refresh interval)
   - If count > 5 OR estimated time > 5 seconds: show confirmation dialog
     - "Refreshing SatNOGS data for 42 satellites. This may take 10-30 seconds. Continue?"
   - If count ≤ 5: proceed immediately with a brief toast notification

2. **During refresh:**
   - Disable the button
   - Show progress indicator: "Refreshing: 12/42 satellites..."
   - Update in real-time as each satellite completes

3. **On completion:**
   - Update "Last refresh" timestamp
   - Update status: "Data current for X/Y satellites"
   - Re-render the Satellites table if user is on that tab

#### Auto-Refresh on Page Load

- On page load, check if any cached SatNOGS data is older than the configured refresh interval
- If yes, automatically trigger a refresh (no confirmation needed for auto-refresh)
- Show a subtle notification: "Auto-refreshing SatNOGS data..."

### 4.4 Settings / Configuration

Add the following settings (stored in localStorage):

| Setting | Key | Default | Type |
|---------|-----|---------|------|
| SatNOGS refresh interval | `satNOGSRefreshInterval` | 7 | Number (days), range 1-30 |
| Show only active satellites | `satNOGSFilterActive` | false | Boolean |

These could be added to an existing settings area or a new "Data Sources" section in the Manage tab.

---

## 5. Implementation Steps

### Phase 1: Core Infrastructure

1. **Add SatNOGS API fetch functions:**
   - `fetchSatelliteStatus(noradId)` — fetches satellite info
   - `fetchTransmitters(noradId)` — fetches active transmitters
   - `fetchSatNOGSData(noradId)` — combines both, returns structured data

2. **Add localStorage management:**
   - `getSatNOGSData(noradId)` — retrieve cached data
   - `setSatNOGSData(noradId, data)` — cache data with timestamp
   - `isDataStale(noradId)` — check if cache exceeds refresh interval
   - `getAllCachedSatellites()` — list all NORAD IDs with cached data

3. **Add helper functions:**
   - `formatFrequency(hz)` — converts Hz to MHz string with 3 decimals
   - `formatFreqRange(lowHz, highHz)` — formats as "X.XXX" or "X.XXX-Y.YYY"
   - `getSatelliteActivity(noradId)` — returns "active" | "no-signals" | "re-entered"
   - `getActivityDot(status)` — returns HTML for status dot

### Phase 2: Satellites Tab

4. **Update satellite table rendering:**
   - Add new columns (Status, DL Freq, UL Freq, Mode)
   - Implement multi-row display for satellites with multiple transmitters
   - Add status dots based on activity classification

5. **Add active satellites filter:**
   - Add toggle UI element
   - Implement filter logic in table rendering
   - Persist toggle state in localStorage

6. **Add fetch-on-demand:**
   - When user adds a satellite (via CelesTrak or paste), fetch SatNOGS data for it
   - Show a loading indicator while fetching
   - Gracefully handle failures (satellite not in SatNOGS DB)

### Phase 3: Tracking Tab

7. **Add frequency/mode display:**
   - Render primary transmitter info below satellite name
   - Implement expandable list for multiple transmitters
   - Handle case where no SatNOGS data is available (show "Frequency info unavailable")

### Phase 4: Manage Tab

8. **Add SatNOGS data section:**
   - Refresh interval setting (input with min/max validation)
   - Last refresh timestamp display
   - Refresh button with progress feedback
   - Status summary (X/Y satellites current)

9. **Implement refresh logic:**
   - Count satellites needing refresh
   - Show confirmation dialog if needed
   - Sequential fetch with progress updates
   - Handle rate limiting and errors gracefully

10. **Implement auto-refresh on load:**
    - Check for stale data
    - Trigger silent refresh if needed
    - Show subtle notification

### Phase 5: Pass Predictions Tab

11. **Update pass predictions:**
    - When "Show only active satellites" is ON, exclude inactive satellites from pass predictions
    - Apply filter consistently across all tabs

### Phase 6: Testing and Polish

12. **Test scenarios:**
    - Satellite with multiple transmitters (e.g., ISS)
    - Satellite with no active transmitters (e.g., AO-91)
    - Re-entered satellite
    - Satellite not in SatNOGS DB
    - Network failure during refresh
    - Rate limiting from SatNOGS API

13. **UI polish:**
    - Ensure frequency columns are monospace and right-aligned (consistent with existing style)
    - Status dots should be subtle but visible
    - Loading states should be clear but not intrusive

---

## 6. Error Handling & Edge Cases

| Scenario | Handling |
|----------|----------|
| Satellite not in SatNOGS DB | Show "?" in status column, "Not available" for frequencies |
| Network failure during fetch | Retry once, then show error toast, keep existing cached data |
| SatNOGS API rate limiting | Respect Retry-After header, show "Rate limited, retrying..." message |
| Mixed data (some cached, some stale) | Show mixed status: "Data current for X/Y satellites" |
| User removes satellite | Clean up cached SatNOGS data for that NORAD ID |
| Empty transmitters list | Classify as "no-signals" (yellow dot) |

---

## 7. Version Bump

After implementation, bump version to **v1.3.0** (minor version increment for new feature set).

---

## 8. Notes & Considerations

- **CORS:** SatNOGS DB API is CORS-enabled, so browser fetches work directly.
- **Rate limiting:** SatNOGS doesn't document strict limits, but we should be respectful — batch requests with small delays if refreshing many satellites.
- **Data accuracy:** SatNOGS data is community-maintained and may not be 100% accurate. We should make it clear this is "SatNOGS data" not official info.
- **Offline use:** The tracker should work without SatNOGS data (graceful degradation). Frequencies simply won't be shown if no cached data exists.
- **Privacy:** All data stays in the user's browser (localStorage). No data is sent from our app to any server except SatNOGS API requests.

---

## 9. Session Handoff

This document will be provided in the next session to continue implementation. The next session should:

1. Load this document as context
2. Review the current state of `satellite-tracker/index.html`
3. Implement Phase 1 (Core Infrastructure) first
4. Test incrementally after each phase

**Project path:** `/Users/keithl/Documents/Arduino/ycint/satellite-tracker/`  
**Main file:** `index.html`  
**Plan file:** `SATNOGS_INTEGRATION_PLAN.md`
