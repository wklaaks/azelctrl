# Yaesu Rotator Controller — User Manual

## 1. Overview

This is a WiFi-enabled rotator controller for Yaesu antennas, built on an ESP8266 (NodeMCU/Wemos D1 Mini). It provides:

- **Dual-axis motor control** (Azimuth and Elevation) via physical buttons or a web browser
- **Real-time position display** on a 128x64 OLED screen
- **Potentiometer-based position sensing** via ADS1115 ADC
- **Potentiometer calibration system** with voltage monitoring and safety limits
- **Direction-reverse safety pause** to prevent motor damage
- **Web interface** for remote control from any device on the same WiFi network

---

## 2. Hardware Components

| Component | Connection | Purpose |
|-----------|-----------|---------|
| ESP8266 (NodeMCU / Wemos D1 Mini) | Main controller | WiFi + motor control logic |
| SSD1306 OLED (128x64) | I2C: SDA=D6, SCL=D7 | Status display |
| ADS1115 ADC | I2C (shared bus) | Potentiometer voltage reading |
| Button 1 (Btn1) | D8 (GPIO15), active HIGH | Menu navigation / calibration exit |
| Button 2 (Btn2) | D5 (GPIO14), active LOW | Movement activation / calibration control |
|| Azimuth motor driver | Enable=D2, Direction=D3 | Controls horizontal rotation |
|| Elevation motor driver | Enable=D1, Direction=D4 | Controls vertical tilt |
| Azimuth potentiometer | ADS1115 Channel 0 (A0) | Horizontal position feedback |
| Elevation potentiometer | ADS1115 Channel 1 (A1) | Vertical position feedback |

---

## 3. Buttons

### Physical Buttons

| Button | Pin | Function |
|--------|-----|----------|
| **Btn1** (D8) | GPIO15 | **Normal mode:** Cycles through menu options (Left → Right → Up → Down → AZ POT → EL POT → Idle)<br>**Calibration mode:** Exits calibration immediately |
| **Btn2** (D5) | GPIO14 | **Normal mode:** Activates the currently selected direction (hold to move, release to stop)<br>**Calibration mode:** Short press = stop scan / check voltage; Long press (≥500ms) = reverse direction |

### Button Behavior Summary

| Mode | Btn1 | Btn2 (short press < 500ms) | Btn2 (long press ≥ 500ms) |
|------|------|---------------------------|---------------------------|
| **Normal** | Cycle menu | Start selected direction | N/A |
| **Calibration (scanning)** | Exit calibration | Stop scan, check voltage | Reverse direction |
| **Calibration (ready)** | N/A | Start scan | N/A |
| **Calibration (result)** | Exit calibration | Re-scan / continue | N/A |

---

## 4. OLED Display Screens

### 4.1 Startup Screens

**WiFi Connecting:**
```
WiFi Connecting

SSID: RaslouwTP

Waiting: 5s
```
Shows WiFi connection progress with elapsed time.

**WiFi Connected:**
```
WiFi Connected!

IP: 192.168.x.x

SSID: RaslouwTP

RSSI: -65 dBm

Press button to continue...
```
Shows connection details. Press Btn1 or Btn2 (or wait 30 seconds) to proceed.

**WiFi Failed:**
```
WiFi FAILED!

SSID: RaslouwTP

Status: 6

Press button to continue...
```
WiFi connection timed out. Press Btn1 or Btn2 (or wait 30 seconds) to proceed without WiFi.

### 4.2 Normal Operation Screen

```
Rotator Control
---------------
Az: 125.3d
El: 45.2d
Move: RIGHT

Sel: LEFT
Btn1:Cycle  Btn2:Go
```

| Line | Content |
|------|---------|
| 1 | Screen title: "Rotator Control" |
| 2 | Separator line |
| 3 | Azimuth position (degrees) — sourced from AZ potentiometer |
| 4 | Elevation position (degrees) — sourced from EL potentiometer |
| 5 | Current movement direction (or "IDLE" when stopped) |
| 6 | Currently selected menu option |
| 7 | Button instructions |

### 4.3 Safety Pause Screen

```
SAFETY PAUSE
------------
AZ axis

Remaining: 342ms
Resume: RIGHT

Az: 125.3d El: 45.2d
```

**When this appears:** The controller detected a direction reversal request on one or both axes. A 600ms safety pause is enforced to allow motor momentum to settle before reversing direction.

| Line | Content |
|------|--------|
| 1 | "SAFETY PAUSE" — emergency state indicator |
| 2 | Separator line |
| 3 | Which axis is paused: "AZ axis", "EL axis", or "Both axes" |
| 4 | Time remaining in pause (counts down) |
| 5 | Direction movement will resume in |
| 6 | Current positions (shown during pause) |

### 4.4 Calibration Screens

#### 4.4a Calibration Ready (just entered)

```
CAL: AZ POT
-----------
AZ: 0.5V  EL: 0.3V

Btn2:Start
Btn2+:Reverse
```

| Line | Content |
|------|--------|
| 1 | Calibration mode and axis: "CAL: AZ POT" or "CAL: EL POT" |
| 2 | Separator line |
| 3 | **Both** potentiometer voltages (AZ and EL) — always shown so you know which pot you're adjusting |
| 4 | Instruction to start scan |
| 5 | Instruction that long press reverses direction |

#### 4.4b Calibration Scanning (in progress)

```
CAL: AZ POT
-----------
AZ: 0.5V  EL: 0.3V
Dir: RIGHT

Scan: 3s left
Btn2:Stop
Btn2+:Reverse
```

| Line | Content |
|------|--------|
| 1 | Calibration mode and axis |
| 2 | Separator line |
| 3 | Both voltages (AZ and EL) |
| 4 | Current movement direction (RIGHT/LEFT for AZ, UP/DOWN for EL) |
| 5 | Countdown timer (5-second scan duration) |
| 6 | Short press Btn2 to stop scan |
| 7 | Long press Btn2 to reverse direction |

#### 4.4c Voltage Too High

```
CAL: AZ POT
-----------
AZ: 1.8V  EL: 0.3V

VOLTAGE HIGH!
Turn AZ pot CCW
```

| Line | Content |
|------|--------|
| 1-3 | Mode, separator, both voltages |
| 4 | Warning: voltage exceeds 1.65V threshold |
| 5 | Instruction: turn the **calibrated axis** pot counter-clockwise (CCW) to decrease voltage |

#### 4.4d Voltage Too Low

```
CAL: AZ POT
-----------
AZ: 0.05V  EL: 0.3V

VOLTAGE LOW
Turn AZ pot CW
```

| Line | Content |
|------|--------|
| 1-3 | Mode, separator, both voltages |
| 4 | Warning: voltage below 0.1V threshold |
| 5 | Instruction: turn the **calibrated axis** pot clockwise (CW) to increase voltage |

#### 4.4e Calibration Complete

```
CAL: AZ POT
-----------
AZ: 0.8V  EL: 0.3V

CAL COMPLETE
V: OK
Btn2:Continue
Btn1:Exit
```

| Line | Content |
|------|--------|
| 1-3 | Mode, separator, both voltages (showing final reading) |
| 4 | Confirmation: calibration successful |
| 5 | Voltage status: OK (within 0.1V–1.65V range) |
| 6 | Press Btn2 to re-scan (continue testing at higher angles) |
| 7 | Press Btn1 to exit calibration mode |

---

## 5. Normal Operation

### 5.1 Starting Up

1. Power on the controller
2. OLED shows "WiFi Connecting" with progress
3. After connecting, OLED shows IP address and "Press button to continue..."
4. Press **Btn1** or **Btn2**, or wait 30 seconds for auto-proceed
5. OLED switches to "Rotator Control" screen

### 5.2 Controlling the Rotator

#### Using Physical Buttons

1. Press **Btn1** repeatedly to cycle through directions:
   - Idle → Left → Right → Up → Down → AZ POT → EL POT → (back to Idle)
2. Press and **hold Btn2** to move in the selected direction
3. **Release Btn2** to stop

#### Using the Web Interface

1. Open a web browser on any device connected to the same WiFi network
2. Navigate to the ESP8266 IP address (shown on OLED after startup)
3. Use the on-screen directional buttons:
   - **UP / LEFT / RIGHT / DOWN** — press and hold to move
   - **STOP** (red button) — stop all movement
4. Buttons light up (cyan) when movement is active
5. Azimuth and Elevation positions update in real-time

### 5.3 Movement Behavior

| Action | Result |
|--------|--------|
| Press Btn2 (or web button) | Motor starts in selected direction |
| Release Btn2 (or web button) | Motor stops |
| Request opposite direction while moving | 600ms safety pause, then reverse |
| Safety pause active | OLED shows "SAFETY PAUSE" with countdown |

**Per-axis independent pause:** If you reverse azimuth while elevation is idle (or vice versa), only the reversing axis pauses. The other axis continues moving.

---

## 6. Potentiometer Calibration

### 6.1 Why Calibrate?

The ADS1115 ADC reads voltage from the potentiometers to determine antenna position. Calibration ensures:
- The voltage range matches the physical pot travel (0° to 360° for azimuth, 0° to 90° for elevation)
- Voltage never exceeds 3.3V (which could damage the ADS1115)
- The ADC reading accurately reflects antenna position at all angles

### 6.2 Calibration Procedure

#### Step 1: Enter Calibration Mode

1. Press **Btn1** to cycle menu until you see "AZ POT" (for azimuth) or "EL POT" (for elevation)
2. Press **Btn2** to enter calibration mode
3. OLED switches to calibration screen showing both voltages

#### Step 2: Start the Scan

1. OLED shows: "Btn2:Start" and "Btn2+:Reverse"
2. Press **Btn2** to begin the 5-second scan
3. The axis starts moving (AZ→RIGHT or EL→UP)
4. OLED shows countdown: "Scan: 5s left"

#### Step 3: Monitor Voltage

During the 5-second scan, the controller:
- Moves the axis for 5 seconds
- Continuously reads **both** AZ and EL voltages
- Displays them on the OLED in real-time
- Shows the current direction (e.g., "Dir: RIGHT")

#### Step 4: Check Results

After 5 seconds (or when you press Btn2 to stop early), the OLED shows one of:

| Result | Meaning | Action |
|--------|---------|--------|
| **VOLTAGE HIGH!** | Voltage > 1.65V — pot set too high | Turn the calibrated axis pot **counter-clockwise (CCW)** to decrease voltage |
| **VOLTAGE LOW** | Voltage < 0.1V — pot set too low | Turn the calibrated axis pot **clockwise (CW)** to increase voltage |
| **CAL COMPLETE / V: OK** | Voltage in safe range (0.1V–1.65V) | Calibration successful |

#### Step 5: Adjust and Re-test (if needed)

If voltage was HIGH or LOW:
1. Adjust the potentiometer as instructed
2. Press **Btn2** to restart the scan
3. The axis moves again for 5 seconds
4. Check voltage again
5. Repeat until voltage is in the OK range

**Why re-test?** A voltage that reads "OK" at low angles may exceed limits at higher angles. Keep re-testing until you've verified the full range.

#### Step 6: Continue Testing (optional)

If calibration is complete but you want to verify at higher angles:
- Press **Btn2** to re-scan from the current position
- The axis continues moving in the same direction
- This lets you verify voltage stays within limits across the full travel

#### Step 7: Reverse Direction (optional)

During any scan, if you want to test the opposite direction:
- **Hold Btn2 for 500ms or more** (long press)
- The axis reverses direction immediately
- OLED updates to show new direction
- Scan continues for another 5 seconds

#### Step 8: Exit Calibration

- Press **Btn1** to exit calibration mode at any time
- Motors stop
- OLED returns to "Rotator Control" screen

### 6.3 Calibration Safety Features

| Feature | Description |
|---------|-------------|
| **Hard voltage limit** | If voltage exceeds 3.3V during scan, movement stops immediately |
| **Warning threshold** | At 1.65V (half of 3.3V), user is warned to adjust pot |
| **Both voltages always shown** | Even when calibrating one axis, the other axis voltage is displayed so you know you're adjusting the correct pot |
| **Axis-specific instructions** | OLED says "Turn AZ pot" or "Turn EL pot" — never ambiguous |
| **Direction display** | Current movement direction is always shown during scanning |

---

## 7. Web Interface

### 7.1 Accessing the Web UI

1. Connect to the same WiFi network as the ESP8266 (SSID: "RaslouwTP")
2. Find the IP address shown on the OLED after startup
3. Open a web browser and navigate to `http://<IP_ADDRESS>`

### 7.2 Web Interface Layout

```
┌─────────────────────────┐
│    Rotator Control      │  ← Title
├─────────────────────────┤
│ Az: 125.3°              │  ← Azimuth position
│ El: 45.2°               │  ← Elevation position
│ Move: RIGHT             │  ← Current movement
│ Status: OK              │  ← PAUSE if safety pause active
├─────────────────────────┤
│         UP              │  ← Move up (hold to move)
│ LEFT  STOP  RIGHT       │  ← ← → directions (hold to move)
│         DOWN            │  ← Move down (hold to move)
└─────────────────────────┘
```

### 7.3 Web Controls

| Control | Action |
|---------|--------|
| **UP / LEFT / RIGHT / DOWN** (blue buttons) | Press and hold to move in that direction |
| **STOP** (red button) | Stop all movement immediately |
| **Direction buttons** | Light up cyan when movement is active (physically or web-initiated) |

### 7.4 Web UI Features

- **Auto-refresh:** Positions and status update every 300ms
- **Button sync:** Physical button presses are reflected in the web UI (buttons light up)
- **Touch support:** Works on mobile devices (touchstart/touchend events)
- **Responsive layout:** Adapts to screen size

---

## 8. Safety Features

### 8.1 Direction Reverse Pause

**What it does:** When you request movement in the opposite direction of current motor rotation, the controller inserts a 600ms pause before reversing.

**Why it matters:** Motors have inertia. Reversing direction instantly can:
- Damage motor windings
- Stress mechanical linkages
- Cause current spikes

**What you see:** OLED displays "SAFETY PAUSE" with a countdown timer. Movement resumes automatically when the pause completes.

**Per-axis independence:** If only one axis is reversing, only that axis pauses. The other axis continues moving normally.

### 8.2 Voltage Monitoring

**During calibration:**
- Voltage is read every ~100ms
- Warning at 1.65V (half the 3.3V max)
- Hard stop at 3.3V (ADS1115 damage threshold)

**During normal operation:**
- Potentiometer positions are read every 200ms
- Readings are clamped to valid range (0–32767 ADC counts)

### 8.3 Safe Boot State

On power-up, all motors are disabled:
- AZ_ENABLE = LOW
- AZ_DIR_CW = HIGH (default direction)
- EL_ENABLE = LOW
- EL_DIR_UP = HIGH (default direction)

Motors only activate when explicitly commanded via button or web interface.

---

## 9. Menu System

### 9.1 Menu Options (cycled with Btn1)

| Index | Display Name | Function |
|-------|-------------|----------|
| 0 | IDLE | No movement (default) |
| 1 | LEFT | Azimuth counter-clockwise (left) |
| 2 | RIGHT | Azimuth clockwise (right) |
| 3 | UP | Elevation up |
| 4 | DOWN | Elevation down |
| 5 | AZ POT | Enter azimuth potentiometer calibration |
| 6 | EL POT | Enter elevation potentiometer calibration |

### 9.2 Menu Navigation Flow

```
[IDLE] → [LEFT] → [RIGHT] → [UP] → [DOWN] → [AZ POT] → [EL POT] → [IDLE] → ...
  ↑                                                                  |
  └──────────────────────────────────────────────────────────────────┘
```

Press Btn1 repeatedly to cycle forward. The menu wraps around from EL POT back to IDLE.

---

## 10. Troubleshooting

### 10.1 OLED Shows Nothing

| Possible Cause | Solution |
|---------------|----------|
| I2C connection loose | Check SDA=D6 and SCL=D7 connections |
| OLED not powered | Verify 3.3V and GND connections |
| Wrong I2C address | OLED should be at address 0x3C |

### 10.2 Motors Not Moving

| Possible Cause | Solution |
|---------------|----------|
| Motor enable pins not connected | Verify D1 (AZ enable) and D2 (EL enable) |
| Motor driver not powered | Check external motor power supply |
| Direction reverse pause active | Wait for "SAFETY PAUSE" countdown to complete |
| Safety pause stuck | Cycle power to reset |

### 10.3 ADS1115 Not Found

| Possible Cause | Solution |
|---------------|----------|
| I2C bus conflict | Ensure no other devices share the I2C bus at conflicting addresses |
| Wiring issue | Check A0 (Ch0) and A1 (Ch1) connections to potentiometers |
| Wrong I2C address | ADS1115 default address is 0x48; check address jumpers |

### 10.4 WiFi Connection Fails

| Possible Cause | Solution |
|---------------|----------|
| Wrong SSID/password | Check `ssid` and `password` in the code |
| Router issues | Verify the ESP8266 can reach the router |
| Signal too weak | Move router closer or add a WiFi repeater |
| Auto-proceed | Wait 30 seconds — the controller will proceed without WiFi |

### 10.5 Calibration Voltage Always High/Low

| Symptom | Solution |
|---------|----------|
| Voltage stays high even after CCW adjustment | Verify you're adjusting the **correct** pot (AZ or EL) — both voltages are shown on screen |
| Voltage stays low even after CW adjustment | Potentiometer may be wired backwards — swap the 3.3V and GND connections |
| Voltage jumps erratically | Check potentiometer wiper connection — may be loose or intermittent |

### 10.6 Position Readings Incorrect

| Symptom | Solution |
|---------|----------|
| Positions don't match physical antenna position | Recalibrate potentiometers (Section 6) |
| Position stuck at 0 or max | Potentiometer may be at physical end-stop — check mechanical limits |
| Position jumps between values | ADC noise — verify potentiometer wiring and consider adding a small capacitor across the wiper and GND |

---

## 11. Technical Reference

### 11.1 Pin Mapping

| Function | Pin | GPIO | Notes |
|----------|-----|------|-------|
| Azimuth Enable | D1 | GPIO5 | Active HIGH |
| Azimuth Direction | D3 | GPIO0 | LOW = CW, HIGH = CCW |
| Elevation Enable | D2 | GPIO4 | Active HIGH |
| Elevation Direction | D4 | GPIO2 | LOW = UP, HIGH = DOWN |
| Button 1 | D8 | GPIO15 | Active HIGH |
| Button 2 | D5 | GPIO14 | Active LOW (internal pull-up) |
| I2C SDA | D6 | GPIO12 | Shared with OLED and ADS1115 |
| I2C SCL | D7 | GPIO13 | Shared with OLED and ADS1115 |

### 11.2 Calibration Thresholds

| Threshold | Value | Description |
|-----------|-------|-------------|
| Hard max | 3.3V (3300mV) | ADS1115 absolute maximum — immediate stop |
| Warning | 1.65V (1650mV) | Half of max — user warned to adjust pot |
| Safe range | 0.1V – 1.65V | Acceptable calibration result |
| Too low | < 0.1V (100mV) | Pot needs adjustment in opposite direction |

### 11.3 Timing Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Update interval | 400ms | OLED refresh rate |
| ADC read interval | 200ms | Potentiometer position reading rate |
| Calibration scan duration | 5000ms (5s) | Movement time per scan cycle |
| Reverse pause | 600ms | Safety delay before direction reversal |
| Long press threshold | 500ms | Btn2 held duration to trigger reverse in calibration |
| WiFi connect timeout | 15s | Maximum time to wait for WiFi connection |
| Startup button wait | 30s | Timeout before auto-proceeding past startup screen |
| Web UI refresh | 300ms | Status polling interval |

### 11.4 Web API Endpoints

| Endpoint | Method | Parameters | Response | Description |
|----------|--------|------------|----------|-------------|
| `/` | GET | — | HTML page | Main control interface |
| `/cmd?dir=LEFT` | GET | `dir`: LEFT, RIGHT, UP, DOWN, STOP | `OK` | Send movement command |
| `/status` | GET | — | JSON: `{az, el, move, pause}` | Get current status |
| `/btn` | GET | — | JSON: `{btn2, move, webMove}` | Get button state and web movement |

### 11.5 Network Configuration

| Setting | Value |
|---------|-------|
| Mode | WiFi Station (STA) |
| SSID | RaslouwTP |
| Password | raslouw666 |
| Port | 80 (HTTP) |
| IP Assignment | DHCP (from router) |

---

## 12. Quick Reference Card

### Everyday Use

1. **Move antenna:** Btn1 to select direction → hold Btn2 to move
2. **Stop:** Release Btn2
3. **Web control:** Open browser → go to IP → click/drag buttons
4. **Check position:** Look at OLED — Az and El lines show current angles

### Calibration

1. **Select axis:** Btn1 until "AZ POT" or "EL POT"
2. **Enter calibration:** Btn2
3. **Start scan:** Btn2
4. **Check voltage:** OLED shows AZ and EL voltages
5. **Adjust pot:** If voltage HIGH → CCW; if LOW → CW
6. **Re-test:** Btn2 to scan again
7. **Reverse direction:** Hold Btn2 ≥ 500ms
8. **Exit:** Btn1

### Safety

- **Direction reverse pause:** Automatic 600ms delay — wait for it
- **Voltage limit:** 3.3V hard max — never exceed this during calibration
- **Always watch both voltages:** Even when calibrating one axis, the other is shown

---

*Document generated from source code — version based on integration_test.ino*
