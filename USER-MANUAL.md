# AZELCtrl — Satellite Rotator Controller User Manual

**Version:** 1.5.2  
**Device:** Yaesu Rotator Controller with ESP8266  
**Target:** Amateur Radio Satellite Tracking & EME (Moonbounce)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Getting Started](#2-getting-started)
3. [Header Bar](#3-header-bar)
4. [Tabs Overview](#4-tabs-overview)
5. [Map Tab](#5-map-tab)
6. [Tracking Tab](#6-tracking-tab)
7. [Satellites Tab](#7-satellites-tab)
8. [Manage Tab](#8-manage-tab)
9. [Passes Tab](#9-passes-tab)
10. [Control Tab](#10-control-tab)
11. [Settings Tab](#11-settings-tab)
12. [Troubleshooting](#12-troubleshooting)
13. [Appendix: Keyboard Shortcuts](#13-appendix-keyboard-shortcuts)

---

## 1. Overview

AZELCtrl is a browser-based satellite rotator controller that runs on an ESP8266 microcontroller. It communicates with a Yaesu rotator (or compatible) to aim azimuth and elevation antennas at satellites, the Sun, or the Moon. All orbital computations run **in your browser** using the satellite.js library — no external servers required.

The web interface is served directly from the ESP8266 over WiFi. The device can operate in **Station (STA)** mode (connects to your home WiFi) or **Access Point (AP)** mode (the device creates its own WiFi network).

**Key capabilities:**
- Track any satellite with TLE data (ISS, CubeSats, amateur radio birds)
- Track the Sun and Moon for EME (moonbounce) or solar calibration
- Automatic AOS/LOS (rise/set) prediction
- Polar plot showing satellite paths across the sky
- Offline world map with satellite footprint, ground track, and terminator
- Hybrid tracking algorithm with kinematic rendezvous and angle-based leading
- ELF (Elevation Flip Mode) mode for continuous passes across the 180° wrap boundary
- Frequency display with Doppler shift calculation from SatNOGS database

---

## 2. Getting Started

### 2.1 Connecting to the Device

On first boot (or after a factory reset), the ESP automatically starts in **Access Point (AP) mode** — no button pressing needed.

1. Power on the AZELCtrl unit.
2. On your computer or phone, open **WiFi settings**.
3. Look for a network named **`AZELCTRL_XXXX`** (where `XXXX` is a 4-digit hex code unique to your device, e.g. `AZELCTRL_A3F1`). Connect to it — password: **`yaesu123`**.
4. Open a browser and navigate to **http://192.168.4.1**.
5. Once connected, go to the **Settings** tab to configure your rotator model, calibration, and home WiFi credentials.

> **Tip:** The web interface is fully functional even while navigating the device's OLED menus — both work independently.

### 2.2 Home Screen

The interface loads automatically. You will see:
- The **header bar** at the top with the application title, satellite selector, TLE age, and connection status.
- A row of **tabs** below the header.
- The **Tracking tab** is shown by default.

---

## 3. Header Bar

The header bar is visible on all screens.

| Element | Description |
|---------|-------------|
| **AZELCtrl Satellite Tracker** | Application title |
| **Satellite dropdown (▸)** | Select any loaded satellite, the Sun, or the Moon. Changes take effect immediately across all tabs. |
| **TLE age (e.g. "2h ago")** | How old the selected satellite's TLE data is. Colored: **fresh** (green, <24h), **stale** (orange, 24–72h), **old** (red, >72h). |
| **Connected / Disconnected** | Green dot = rotator reachable. Red = no connection. |

### Using the Satellite Dropdown

- Click to open the list of satellites sorted alphabetically.
- Use the **↑/↓ arrow keys** to cycle through satellites without opening the menu.
- The first two entries are **☀️ Sun** and **🌙 Moon** — select these for solar/lunar tracking.
- Below them, all loaded satellites are listed.
- Selecting **— select satellite —** deselects the current target.

---

## 4. Tabs Overview

| Tab | Purpose |
|-----|---------|
| **Map** | Interactive world map showing satellite position, ground track, footprint, terminator, Sun/Moon |
| **Tracking** | Real-time satellite/computed tracking with polar plot, AOS/LOS timer, frequency display |
| **Satellites** | Browse and manage satellite passes and frequencies |
| **Manage** | Add, remove, and refresh TLE data for satellites |
| **Passes** | Predict upcoming passes for the selected satellite |
| **Control** | Manual rotator control and calibration |
| **Settings** | Station location, tracking preferences, device configuration (rotator model, calibration, WiFi, protocol, network), ESP management, debug options |

---

## 5. Map Tab

The Map tab displays an offline, equirectangular-projection world map rendered on an HTML Canvas — no external mapping API required.

### Map Overlays

| Toggle | Default | Description |
|--------|---------|-------------|
| **Day/Night** | Off | Shows the day/night terminator (sunrise/sunset lines and shaded night side) |
| **Footprint** | On | Green translucent circle showing the area on Earth where a satellite has ≥0 degree elevation (direct line of sight) |
| **Grid** | On | Latitude/longitude grid lines (every 30° lat, 60° lon) with labels |
| **🌙 Moon** | Off | Shows the Moon's current sub-point as a silver circle |
| **☀️ Sun** | Off | Shows the Sun's current sub-point as an amber circle |

> **Note:** Sun/Moon positions update every 30 seconds and work independently of the Day/Night terminator toggle.

### Map Controls

| Control | Description |
|---------|-------------|
| **Duration** | Selects the ground track duration (30 min to 6 hours). The future track is shown as colored orbit segments. |
| **↺ Reset View** | Centers the map on your ground station and resets zoom to 1x. |
| **⊕ Center Sat** | Centers the map on the current satellite position. |

### Map Interaction

- **Drag** to pan the map.
- **Scroll** (mouse wheel) or **pinch** (touch) to zoom.
- The map footer shows the satellite's **POS** (latitude/longitude), **ALT** (altitude km), **VEL** (orbital velocity km/s), and **DIST** (great-circle distance to station km).

---

## 6. Tracking Tab

The primary tab for operating the rotator. It contains:

### Satellite / Astro Body Info

| Field | Description |
|-------|-------------|
| **SATELLITE Az** | Current azimuth of the target (degrees) |
| **SATELLITE El** | Current elevation of the target (degrees) |
| **AOS in / LOS in** | Time until the next acquisition of signal (rise) or loss of signal (set). For Sun/Moon, shows the next rise/set time. |
| **AOS Az / LOS Az** | Azimuth at the rise or set time |
| **Range** | Distance to the target (km) |

### Rotator Info

| Field | Description |
|-------|-------------|
| **ROTATOR Az** | Current rotator azimuth position |
| **ROTATOR El** | Current rotator elevation position |
| **Movement arrows (● → ← ↑ ↓)** | Indicate direction the rotator is currently moving |

### ELF Mode Toggle

**ELF (Elevation Flip Mode)** mode disables the 180° azimuth soft-stop, allowing the rotator to rotate continuously for passes that cross the 180°/0° wrap boundary. Enable this before starting a pass that crosses the boundary.

### Start / Stop Button

- **Start** — begins tracking the selected target. The system will:
  1. Calculate the current target position.
  2. Compute a kinematic rendezvous for the first move.
  3. Continuously send updated azimuth/elevation commands using angle-based leading.
  4. Automatically stop when the target goes below 0° elevation.

- **Stop** — immediately halts tracking.

### Polar Plot

The circular polar plot shows:
- **Elevation rings** at 10° increments (90° at center, 0° at outer edge).
- **Azimuth labels** every 30°.
- **Satellite trajectory** for the current pass:
  - **Solid blue line** — path already traveled.
  - **Dashed green line** — future path (above minimum elevation).
  - **Orange dashed line** — future path (below minimum elevation, above 0°).
- **Blue diamond** — current satellite/computed position.
- **Green circle** — current rotator position.

### Frequency Display (Satellite mode only)

When tracking a satellite with SatNOGS frequency data, a frequency table shows:
- **DL Freq MHz** — downlink frequencies.
- **RX Doppler** — corrected receive frequency accounting for Doppler shift.
- **UL Freq MHz** — uplink frequencies.
- **TX Comp** — corrected transmit frequency.
- **Mode** — modulation/transmission mode.

---

## 7. Satellites Tab

### Satellite Pass List

Displays all loaded satellites with upcoming pass information. Each row shows:

| Column | Description |
|--------|-------------|
| **Satellite Name** | Name with operating frequency/mode |
| **AOS** | Next rise time |
| **LOS** | Next set time |
| **AOS Az** | Azimuth at rise |
| **Max El** | Maximum elevation during the pass |
| **Duration** | Pass duration |
| **Until** | Countdown timer to AOS or LOS |
| **Status** | Whether the satellite is currently overhead |

### Filters

- **Only Active** — show only satellites with SatNOGS status "active".
- **Freq/Modes** — show frequency and mode information.
- **Freq-Hide** — hide satellites without frequency data.
- **Sat-Hide** — hide individual satellites from the list (click the eye icon).

### Satellite List Navigation

Click a satellite row to select it. The selected satellite becomes the active target for tracking, map, and passes tabs. Use the arrow buttons at the bottom to navigate through the list.

### Sort & Batch Operations

- **Sort by Name / Number** — reorder the satellite list.
- **Refresh All TLEs** — update TLE data for all satellites from CelesTrak.
- **Multi-select** checkboxes allow batch deletion of satellites.

---

## 8. Manage Tab

### Manage Added Satellites

A compact list of all satellites you have added. Each entry shows:
- Satellite name
- TLE age indicator
- Remove button (X)

### Adding Satellites

#### CelesTrak Add Tab
1. Browse the CelesTrak satellite catalog (filtered by your frequency preferences).
2. Click **Add** next to any satellite to import it with TLE data.
3. Use **Add All** to import every satellite in the current view.

#### Paste Add Tab
1. Paste a two-line TLE (or NORAD ID) into the text area.
2. Click **Add** to parse and store the satellite.

#### SatNOGS Data Tab
- Browse satellites from the SatNOGS database with their frequency information.
- Use **Fetch & Add** to download TLE data from CelesTrak for a given NORAD ID.
- Filter satellites by operating frequency band.

> **Note:** TLE data is stored in the browser's local storage. Clearing browser data will remove your satellite list.

---

## 9. Passes Tab

Shows detailed pass predictions for the selected satellite, Sun, or Moon.

| Feature | Description |
|---------|-------------|
| **Lookahead** | Select prediction window: 24, 48, or 72 hours. |
| **Pass table** | Lists each upcoming pass with AOS time, LOS time, AOS azimuth, maximum elevation, and duration. |
| **Sun/Moon** | When selected, displays "No pass data for celestial bodies" — rise/set info is shown in the Tracking tab's AOS/LOS display instead. |

Pass predictions auto-refresh when the satellite or lookahead duration changes.

---

## 10. Control Tab

### Rotator Position
Shows the current azimuth and elevation of the rotator.

### Button Control Pad

A cross-shaped control pad for manual rotator movement. The buttons correspond to compass directions:

| Button | Action |
|--------|--------|
| **UP** | Hold → rotates elevation upward. Release → stops. |
| **DOWN** | Hold → rotates elevation downward. Release → stops. |
| **LEFT** | Hold → rotates azimuth counter-clockwise. Release → stops. |
| **RIGHT** | Hold → rotates azimuth clockwise. Release → stops. |
| **STOP** | Immediately halts all movement. |
| **1.Nav / 1|Sel / 2.Back** | Footer menu for OLED display navigation (if connected). |

### Manual Position

Enter precise azimuth and elevation values, then click **Set** to move the rotator to that exact position.

### Calibration

Click **Start Calibration** to enter calibration mode. Follow the on-screen instructions to set the azimuth and elevation endpoints. The calibration values are stored on the ESP and persist across reboots.

---

## 11. Settings Tab

### Ground Station Location

| Field | Description |
|-------|-------------|
| **Maidenhead Grid Square** | Enter your grid square (e.g., KG44BD) and click **Convert** to auto-fill lat/lon. |
| **Latitude** | Your station's latitude in decimal degrees (negative = South). |
| **Longitude** | Your station's longitude in decimal degrees (negative = West). |
| **Altitude** | Meters above sea level (optional, minor accuracy improvement for low-elevation passes). |

### Tracking Preferences

| Field | Description |
|-------|-------------|
| **Update Interval** | How often the system computes position and sends commands to the rotator. Default: 2 seconds. Lower = tighter tracking but more wear. |
| **Minimum Elevation** | Ignore passes below this angle. Default: 10°. |
| **Azimuth Speed** | Rotator azimuth speed (degrees/second). Default 2.5°. Auto-refined from tracking measurements. |
| **Elevation Speed** | Rotator elevation speed (degrees/second). Default 2.5°. Auto-refined from tracking measurements. |
| **Lead Angle** | Degrees ahead to aim — the antenna moves this many degrees along the satellite's path before waiting. Higher = less movement but lower accuracy. |
| **Dead Zone** | Tolerance around the target before commanding a new position. Higher = fewer commands, smoother movements. |

### Controller

| Field | Description |
|-------|-------------|
| **Rotator Model** | Select your rotator variant: **N360** (0–360° North-center), **S360** (0–360° South-center), **N450** (0–450° North-center), or **S450** (0–450° South-center). Click **Set** to apply. Changing the model clears calibration data. |
| **Calibration** | Displays the current calibration status. **Load from ESP** refreshes the displayed voltage values. **Skip Calibration** sets a default 0–3.2V range (usable for testing). You can also manually enter **Az Min/Max** and **El Min/Max** voltages and click **Set** to apply custom calibration. |

> Calibration defines the voltage range that corresponds to your rotator's full mechanical travel. A proper calibration sweep (via the Control tab or OLED menu) is recommended for accurate tracking.

### WiFi

| Field | Description |
|-------|-------------|
| **WiFi Mode** | Select **Access Point** (ESP creates its own WiFi network), **Station** (connects to your home WiFi), or **Disabled** (WiFi off). Click **Apply** to switch. A confirmation dialog warns you that you will be disconnected from the ESP during the switch. |
| **Station SSID / Password** | Enter your home WiFi network name and password, then click **Save**. These are stored on the ESP. |
| **AP Password** | Change the 8-character password used to connect to the ESP's AP network. Must be exactly 8 characters. |
| **Current AP SSID** | Displays the ESP's AP network name (read-only). |

### PC Interface

| Field | Description |
|-------|-------------|
| **Protocol Variant** | Select the serial protocol: **GS-232A**, **GS-232B**, or **Easycom**. Click **Set** to apply all protocol changes at once. |
| **Baud Rate** | Serial baud rate for the PC interface: 9600, 19200, 38400, 57600, or 115200. Changes take effect immediately. |
| **Show Commands** | When enabled, received commands are displayed on the OLED screen. |
| **Reverse Delay** | Seconds to wait before reversing motor direction (0–3s). Prevents gear backlash. |

### Network

| Field | Description |
|-------|-------------|
| **Hamlib TCP Server** | Toggle the Hamlib `rotctld`-compatible server on port 4533. Enable this to control the rotator from Hamlib-compatible applications (e.g., Gpredict, PstRotator) over TCP. |

### ESP Management

| Feature | Description |
|---------|-------------|
| **Device IP Address** | Leave empty when accessing the device directly. Set this only when routing through a proxy server. |
| **Upload Web Interface** | Select a .html.gz file and click **Upload HTML** to update the web interface without reflashing. |
| **Upload Any File** | Upload images, SVGs, or other assets. Files are accessible at /files/filename. |
| **Files on ESP** | Click **List Files** to see all files stored on the ESP filesystem. You can delete individual files. |

### Debug

| Option | Description |
|--------|-------------|
| **JavaScript Logging** | Enable detailed console logs for troubleshooting. |
| **Download Config** | Downloads a JSON file with all current settings. |
| **Load Config** | Restore settings from a previously downloaded JSON file. |

---

## 12. Troubleshooting

### "Select a satellite first" when pressing Start
Ensure a target is selected in the header dropdown.

### No map data shown
The world map requires world_coastline.js.gz to be uploaded to the ESP. Use the **Upload Any File** feature in Settings to upload it.

### Satellite not tracking accurately
- Verify your **Ground Station Location** in Settings.
- Check the **Azimuth/Elevation Speed** values match your rotator.
- Ensure the **Lead Angle** and **Dead Zone** are set appropriately (defaults work for most setups).

### Sun/Moon tracking shows incorrect AOS/LOS
The system computes rise/set times astronomically and updates every polling interval. If times seem off, verify your station location is correct.

### Cannot connect to the device
- If in AP mode: connect to the **AZELCTRL** WiFi network.
- If in STA mode: the device will announce its IP on the serial console, or check your router's DHCP client list.
- IP address is typically 192.168.88.154 when connected through the default home network.

### Chrome: page does not load (blank / ERR_CONNECTION_RESET)
Recent Chrome versions (v94+) may block plain HTTP connections to local network devices. Safari and Firefox are unaffected.

**Fix:** In Chrome, navigate to `chrome://flags/#unsafely-treat-insecure-origin-as-secure` and add `http://192.168.88.154` to the list (or your device's IP if different). Enable the flag and restart Chrome.

If the page still doesn't load, check `chrome://flags/#block-insecure-private-network-requests` is set to **Disabled**.

### Browser shows old version
Hard refresh (Cmd+Shift+R / Ctrl+F5) to clear the browser cache. The version number is shown in the footer.

---

## 13. Appendix: Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| **Up/Down arrows** (when dropdown focused) | Cycle through satellites |
| **Ctrl+Shift+R** | Hard refresh browser cache |
| **F12** | Open browser developer console (for debugging) |

---

*AZELCtrl — All orbital computation runs in your browser. No external servers required.*