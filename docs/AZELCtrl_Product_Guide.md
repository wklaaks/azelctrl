# AZELCtrl — Complete Product Guide

**Version 1.0**  
**Author: ZS6TW**  
**Date: July 2026**

---

## 1. Introduction

### 1.1 What is AZELCtrl?

AZELCtrl is a WiFi-enabled, dual-axis rotator controller designed for amateur satellite operators using Yaesu elevation-azimuth rotator systems. Built around an ESP8266 microcontroller, it provides precise motor control, real-time position feedback, and network connectivity for integration with popular satellite tracking software such as Gpredict and Rotor.

### 1.2 The Problem It Solves

Yaesu rotator controllers (e.g., G-5600B) provide excellent mechanical performance but lack modern connectivity options. Traditional control requires direct serial connection to a computer, limiting flexibility and remote operation. AZELCtrl bridges this gap by:

- Providing WiFi-based remote control via web browser and TCP/IP protocols
- Enabling seamless integration with satellite tracking software running on any device on the network
- Adding intelligent features such as over-the-top tracking mode for problematic satellite passes
- Offering real-time position monitoring via an integrated OLED display

### 1.3 Target Audience

This product is designed for amateur radio operators who:

- Own a Yaesu azimuth-elevation rotator system (G-550, G-550X, G-5600, G-5600B, or compatible)
- Track low Earth orbit (LEO) satellites and require automated antenna pointing
- Want network-based control without replacing their existing rotator hardware
- Are comfortable with basic electronics assembly and wiring

---

## 2. Key Features & Benefits

### 2.1 Core Features

| Feature | Description |
|---------|-------------|
| **Dual-Axis Motor Control** | Independent control of azimuth and elevation motors with conditional stop logic and direction-reverse safety pause |
| **WiFi Connectivity** | ESP8266-based wireless operation — control from any device on the same network |
| **Web Interface** | Browser-based control panel with directional buttons and real-time position display |
| **TCP/IP Protocol Support** | Compatible with satellite tracking software via TCP port 4533 |
| **Multiple Protocol Variants** | Supports GS-232A, GS-232B, Easycom, and rotctl text protocol |
| **Real-Time Position Display** | 128×64 OLED screen showing azimuth, elevation, movement direction, and system status |
| **Potentiometer Calibration** | Built-in calibration system with voltage monitoring and safety limits |
| **Over-the-Top (ELF) Mode** | Eliminates rotator wrap-around problems during problematic satellite passes |
| **Rotator Model Support** | Configurable for N360, S360, N450, and S450 rotator variants |

### 2.2 Benefits Over Traditional Controllers

- **No direct serial tether required** — operates independently on the network
- **Remote monitoring** — check antenna position from anywhere via web browser
- **Multiple control methods** — physical buttons, web interface, or tracking software
- **Intelligent tracking** — ELF mode automatically handles passes that would otherwise cause the rotator to perform a damaging full rotation
- **Safety mechanisms** — voltage monitoring protects sensitive electronics; direction-reverse pause prevents motor damage
- **Compact and self-contained** — all components fit in a small enclosure

---

## 3. How It Works (High-Level Overview)

### 3.1 System Architecture

AZELCtrl sits between your satellite tracking software and your Yaesu rotator controller:

```
┌─────────────────────┐      TCP/IP       ┌──────────────────┐      Yaesu DIN-8     ┌─────────────────┐
│ Satellite Tracking  │ ◄────────────────► │   AZELCtrl       │ ◄──────────────────► │ Yaesu Rotator   │
│ Software            │  (port 4533)       │                  │                      │ Controller      │
│ (Gpredict, Rotor)   │                    │ ┌──────────────┐ │ ┌──────────────────┐ │ (G-5600B etc.)  │
│                     │                    │ │ ESP8266      │ │ │ Motor Shield     │ │                 │
│                     │                    │ │ (NodeMCU)    │─┼─►│ (L293DD)         │─┼─► Motor relays  │
│                     │                    │ │              │ │ │                  │ │                 │
│                     │                    │ └──────┬───────┘ │ └────────┬─────────┘ │                 │
│                     │                    │        │         │          │           │                 │
│                     │                    │ ┌──────▼───────┐ │ ┌────────▼─────────┐ │                 │
│                     │                    │ │ OLED Display │ │ │ ADS1115 ADC      │◄┤ Position pots   │
│                     │                    │ │ (128×64)     │ │ │                  │ │                 │
│                     │                    │ └──────────────┘ │ └──────────────────┘ │                 │
└─────────────────────┘                    └──────────────────┘                      └─────────────────┘
```

### 3.2 Data Flow

1. **Command Path**: Satellite tracking software sends position commands (azimuth/elevation) over TCP to AZELCtrl on port 4533. The ESP8266 interprets the command according to the configured protocol (GS-232, Easycom, or rotctl), calculates the required motor movements, and sends control signals to the motor shield.

2. **Motor Control Path**: The motor shield (L293DD) receives direction and enable signals from the ESP8266 and switches the Yaesu controller's relay inputs to ground, activating the appropriate rotator motors.

3. **Position Feedback Path**: The Yaesu controller outputs analog voltages proportional to the current azimuth and elevation positions. These voltages are read by the ADS1115 ADC module, converted to digital values by the ESP8266, and displayed on the OLED screen. Position data is also reported back to the tracking software when polled.

### 3.3 Power Flow

- AZELCtrl is powered via USB (5V to VIN) or an external 5V supply.
- Motor drive voltage (12V) is supplied by the Yaesu controller through the DIN-8 connector (Pin 7 to VM).
- **Critical**: VIN (5V logic) and VM (12V motor) must remain electrically isolated. The motor shield's VIN-VM jumper must be removed.

---

## 4. Technical Details

### 4.1 Hardware Components

| Component | Specification | Purpose |
|-----------|--------------|---------|
| **ESP8266 NodeMCU v2** | 80 MHz, 4 MB Flash, built-in WiFi | Main microcontroller — runs firmware, handles protocols, WiFi, motor control |
| **ESP-12E Motor Shield** | L293DD dual full-bridge driver | Provides motor control outputs and I/O expansion headers |
| **SSD1306 OLED Display** | 128×64 pixels, I2C interface | Real-time position display and menu system |
| **ADS1115 ADC Module** | 16-bit, 4-channel, I2C interface | Converts Yaesu potentiometer voltages to digital readings |
| **Push Buttons (×2)** | Momentary, normally open | Physical menu navigation and manual control |
| **Enclosure** | Small plastic project box | Houses all components securely |

### 4.2 Pin Assignments

#### ESP8266 → Motor Shield (Hard-Wired)

| NodeMCU Pin | GPIO | Function | Connected To |
|-------------|------|----------|--------------|
| D1 | GPIO5 | Motor A Enable | Azimuth motor enable |
| D3 | GPIO0 | Motor A Direction | Azimuth direction (CW/CCW) |
| D2 | GPIO4 | Motor B Enable | Elevation motor enable |
| D4 | GPIO2 | Motor B Direction | Elevation direction (Up/Down) |

#### ESP8266 → External Components

| NodeMCU Pin | GPIO | Function | Connected To |
|-------------|------|----------|--------------|
| D6 | GPIO12 | I2C SDA | OLED SDA + ADS1115 SDA |
| D7 | GPIO13 | I2C SCL | OLED SCL + ADS1115 SCL |
| D8 | GPIO15 | Button 1 (active HIGH) | Btn1 → 3.3V (GPIO15 has internal pull-down) |
| D5 | GPIO14 | Button 2 (active LOW) | Btn2 → GND (GPIO14 has internal pull-up) |

#### Motor Shield → Yaesu DIN-8

| Shield Terminal | Yaesu DIN Pin | Function |
|-----------------|---------------|----------|
| A+ | Pin 2 | Azimuth Clockwise |
| A- | Pin 4 | Azimuth Counter-Clockwise |
| B+ | Pin 3 | Elevation Down |
| B- | Pin 5 | Elevation Up |
| VM | Pin 7 | 12V from Yaesu controller |
| GND | Pin 8 | Common ground |

#### ADS1115 → Yaesu DIN-8 (Position Feedback)

| ADS1115 Channel | Yaesu DIN Pin | Function |
|-----------------|---------------|----------|
| A0 | Pin 6 | Azimuth position voltage |
| A1 | Pin 1 | Elevation position voltage |

### 4.3 Motor Control Theory

The L293DD motor driver is configured to control the Yaesu controller's relay inputs. Each Yaesu relay is activated by grounding its control pin. The motor shield provides three states per output:

| State | Shield Pin + | Shield Pin - | Yaesu Relay |
|-------|-------------|-------------|-------------|
| Open circuit | Floating | Floating | De-energized (motor stopped) |
| Ground | VCC | GND | Energized (motor active) |
| High voltage | GND | VCC | De-energized (12V ≈ open circuit for Yaesu) |

The ESP8266 controls direction by selecting which motor shield output is grounded, and enables/disables movement by toggling the enable pin. The shield's design prevents both inputs from being grounded simultaneously, even in the event of a programming error.

### 4.4 I2C Bus Architecture

Both the OLED display and ADS1115 share the I2C bus:

- **SDA**: NodeMCU D6 (GPIO12)
- **SCL**: NodeMCU D7 (GPIO13)
- **OLED address**: 0x3C
- **ADS1115 address**: 0x48 (default, determined by address jumpers)

The ADS1115 operates at 16-bit resolution with a programmable gain amplifier. For our application, the ±4.096V range is used to safely accommodate the Yaesu controller's output voltages (which must be trimmed to ≤3.3V).

### 4.5 Protocol Stack

AZELCtrl runs a TCP server on **port 4533** that accepts connections from satellite tracking software. The protocol spoken over TCP is determined by the **Hamlib Server** setting in the rotator menu:

| Hamlib Server | TCP Protocol |
|---------------|-------------|
| **Yes** | rotctl text protocol (`p`, `P`, `S` commands) |
| **No** | Configured serial protocol (GS-232A, GS-232B, or Easycom) |

The server always listens on port 4533 regardless of the Hamlib Server setting — the setting only determines which protocol dialect is used for communication.

#### GS-232 Command Reference

| Command | Description | Response |
|---------|-------------|----------|
| `C` | Query current position | `Caaa.ee\r` (AZ=aaa, EL=ee) |
| `Maaa` | Move azimuth to aaa degrees | `OK\r` |
| `Waaa eee` | Move to azimuth aaa, elevation eee | `OK\r` |
| `L` | Move azimuth left (CCW) | `OK\r` |
| `R` | Move azimuth right (CW) | `OK\r` |
| `U` | Move elevation up | `OK\r` |
| `D` | Move elevation down | `OK\r` |
| `S` | Stop all movement | `OK\r` |

#### rotctl Command Reference

| Command | Description | Response |
|---------|-------------|----------|
| `p` | Query position | `az\nel\nRPRT 0\n` |
| `P az el` | Move to position | `RPRT 0\n` |
| `S` | Stop | `RPRT 0\n` |

### 4.6 Coordinate Transformations

#### Rotator Model Variants

AZELCtrl supports four rotator model configurations:

| Model | Range | Wrap Boundary | Typical Use |
|-------|-------|---------------|-------------|
| **N360** | 0°–360° (North-centered) | 0°/360° (North) | Northern Hemisphere |
| **S360** | 0°–360° (South-centered) | 180° (South) | Southern Hemisphere |
| **N450** | 0°–450° (North-centered) | 0°/360° (North) | Northern Hemisphere, extended range |
| **S450** | 0°–450° (South-centered) | 180° (South) | Southern Hemisphere, extended range |

The selected model affects how the firmware interprets raw ADC readings and calculates shortest-path movement directions.

#### Auto-Stop Logic

AZELCtrl implements mechanical offset tracking for extended-range models:

- **N360**: Auto-stops at 180° (South)
- **S360**: Auto-stops at 0° (North)
- **N450**: Auto-stops at 135° and 225° (overlap zone)
- **S450**: Auto-stops at 315° and 45° (overlap zone)

A configurable reverse delay (0–3 seconds, default 1 second) is applied when changing direction to allow motor momentum to settle.

#### Preventing Tracking Oscillations

When AZELCtrl receives position targets from tracking software, it must decide when the rotator has reached its destination and stop the motors. This is not as simple as it sounds due to mechanical inertia.

**The problem**: When motor power is cut, the rotator does not stop instantly. Mechanical momentum carries it past the target point before friction brings it to rest. If AZELCtrl waits until the measured position matches the target exactly before cutting power, the rotator will consistently overshoot by several degrees. The tracking software then sends a correction command to move back toward the target, the rotator overshoots again in the opposite direction, and the system enters a continuous ±3–5° oscillation.

**The solution**: AZELCtrl uses a **3.0° auto-stop tolerance** for protocol-controlled tracking. When the current position is within 3.0° of the target, the motors are powered off early enough that mechanical inertia carries the rotator to within the target zone rather than past it. This effectively "predicts" the overshoot and compensates for it.

The position feedback loop runs at 100ms intervals (ADC reads every 100ms, auto-stop checks every 100ms), giving AZELCtrl timely awareness of the rotator's position while avoiding excessive CPU load. The 3.0° tolerance was chosen empirically: it is large enough to prevent oscillation during normal tracking speeds, yet small enough to maintain acceptable pointing accuracy for satellite contacts.

### 4.7 EEPROM Layout

Configuration data is persisted in EEPROM across power cycles:

| Bytes | Size | Content | Magic |
|-------|------|---------|-------|
| 0–66 | 67 | WiFi SSID and password | 0xAA |
| 67–83 | 17 | Calibration values (min/max voltages) | 0xCA |
| 84–85 | 2 | WiFi mode (STA/AP/Disabled) | — |
| 86–87 | 2 | Network configuration | — |
| 88–89 | 2 | Rotator model (N360/S360/N450/S450) | — |
| 90 | 1 | ELF mode enabled/disabled | — |

### 4.8 Safety Mechanisms

- **Direction-reverse pause**: 600ms pause when reversing motor direction to allow momentum to settle. Per-axis independent — reversing azimuth while elevation is idle only pauses azimuth.
- **Voltage monitoring**: During calibration, voltages are checked every ~100ms. Warning at 1.65V, hard stop at 3.3V (ADS1115 damage threshold).
- **Conditional motor stops**: Motors are only stopped when changing direction or stopping entirely — not when continuing in the same direction. This prevents unnecessary engagement delays that could cause tracking software timeouts.
- **Engagement delay**: 200ms delay after motor re-engagement to mask mechanical backlash without triggering client timeouts.
- **Safe boot state**: On power-up, all motors are disabled. No movement occurs until explicitly commanded.

---

## 5. Over-the-Top (ELF) Tracking Mode

### 5.1 The Wrap-Around Problem

Amateur satellite operators routinely track low Earth orbit (LEO) satellites using azimuth-elevation rotator systems. In most passes, the rotator simply follows the satellite across the sky without incident. However, a particular class of pass presents a serious mechanical challenge: those in which the satellite crosses the rotator's **wrap-around boundary** — the azimuth heading at which the rotator must reverse direction or rotate nearly a full 360 degrees to continue tracking.

Consider a Southern Hemisphere operator using a south-centered rotator. A satellite rises in the southwest at Az=225°, climbs to a peak near Az=180° (due south), and sets in the southeast at Az=135°. The rotator tracks perfectly until the satellite approaches 180°. To continue tracking, the controller would need to move from, say, Az=181° to Az=179° — but the rotator's mechanical range ends at ±180°. To reach 179°, the drive motor must reverse direction and travel nearly 360° clockwise (past north, east, south, and back). On a typical rotator with a slew speed of around 4 degrees per second, this journey takes well over a minute.

During that minute, the antenna is pointed nowhere near the satellite. For a voice or data contact, this is catastrophic. The link drops, the pass is lost, and the opportunity is wasted.

The equivalent problem for Northern Hemisphere operators occurs when a satellite passes to their north — rising at Az=330° and setting at Az=90°, crossing through Az=0°/360°. The mechanics are identical, and the solution is the same.

### 5.2 The Geometric Principle

Most amateur-grade elevation rotators designed for satellite work support elevation movement not just from 0° (horizon) to 90° (zenith), but all the way to 180°. An elevation of, for example, 135° means the antenna is tilted past vertical and is now pointing toward a direction 45° above the horizon on the **opposite side** of the rotator.

This mechanical capability is the key to the over-the-top technique.

The insight is straightforward: any point in the sky can be reached from two different azimuth headings. If a satellite is at Az=180°, El=35°, you can point your antenna at it directly — or you can point your antenna at Az=0°, El=145°. In the latter case, the antenna is rotated to face north, but elevated past vertical so that it points back over the top toward the south at an angle of 35° above the southern horizon.

The transformation is:

```
Real Azimuth   = (Satellite Azimuth + 180°) mod 360°
Real Elevation = 180° − Satellite Elevation
```

Applied continuously during the pass, this means the azimuth rotator is smoothly tracking through the opposite side of sky — through its comfortable travel range near Az=0° (or Az=180° for the Northern Hemisphere case) — while the elevation rotator holds the antenna pointed at the correct elevation from the other side.

### 5.3 How AZELCtrl Implements ELF Mode

AZELCtrl implements ELF mode as a transparent coordinate transformation layer. When enabled:

1. **Position reports**: When tracking software polls for position (`C` or `p` command), AZELCtrl reads the physical position from the ADC, applies the inverse ELF transformation, and reports the **effective** (satellite-space) coordinates. The tracking software sees the antenna pointing directly at the satellite, even though the rotator is physically in the opposite direction.

2. **Movement commands**: When tracking software sends a movement command (`W`, `P`, `M`), AZELCtrl receives the satellite-space target, transforms it to physical coordinates, and steers the rotator to the translated position.

3. **Direction calculation**: All shortest-path direction calculations are performed in consistent coordinate space to ensure correct movement direction regardless of ELF mode state.

4. **Elevation prompt**: When ELF mode is toggled, AZELCtrl displays an "Elevate To?" prompt allowing the operator to immediately set the effective elevation to Horizon (0°), 45°, 90°, or Skip (no change). This is useful for quickly repositioning the antenna before a pass.

### 5.4 When to Use ELF Mode

The over-the-top transformation should be applied when:

- The satellite pass crosses the rotator's wrap-around boundary
- The pass cannot be tracked in direct mode without the rotator performing a full rotation

The over-the-top transformation should **not** be applied when:

- The pass does not cross the wrap-around boundary (unnecessary mechanical load on the elevation rotator)
- Linear polarisation must be maintained without adjustment (over-the-top mode rotates the polarisation plane by 180°)

### 5.5 Practical Considerations

- **Elevation rotator capability**: Verify that your elevation rotator physically supports movement beyond 90°. The Yaesu G-5600B, for example, supports 0–180° of elevation travel.
- **Pre-positioning before AOS**: For over-the-top passes, the antenna must be pre-positioned to the starting position before the satellite rises. This typically means the elevation will be near 180° while waiting for AOS.
- **Polarisation effects**: Flipping the antenna over the top rotates the polarisation plane by 180°. For circular polarisation this is irrelevant. For linear polarisation, you may need to adjust the polarisation rotator or accept a polarisation mismatch.
- **Decision timing**: The decision to use over-the-top mode must be made **before** the pass begins, based on a pre-computed prediction of the complete azimuth track. Switching modes mid-pass would require a large, sudden azimuth slew that would lose the satellite.

---

## 6. Menu System Reference

### 6.1 Button Behavior Summary

| Button | Pin | Electrical Behavior |
|--------|-----|---------------------|
| **Btn1** | D8 (GPIO15) | Active HIGH — pulls to 3.3V when pressed (internal pull-down) |
| **Btn2** | D5 (GPIO14) | Active LOW — pulls to GND when pressed (internal pull-up) |

**Note on GPIO15**: This pin must be LOW at reset for the ESP8266 to boot normally. The motor shield comes with a fixed pull-down resistor. If you power up the unit while Btn1 is pressed, it will not boot properly.

### 6.2 Startup Screens

#### WiFi Connecting

```
WiFi Connecting

SSID: RaslouwTP

Waiting: 5s
```

Shows WiFi connection progress with elapsed time.

#### WiFi Connected

```
WiFi Connected!

IP: 192.168.x.x

SSID: RaslouwTP

RSSI: -65 dBm

Press button to continue...
```

Shows connection details. Press Btn1 or Btn2, or wait 30 seconds to proceed.

#### WiFi Failed

```
WiFi FAILED!

SSID: RaslouwTP

Status: 6

Press button to continue...
```

WiFi connection timed out. Press Btn1 or Btn2, or wait 30 seconds to proceed without WiFi.

### 6.3 Main Az/El Control Screen

```
Azimuth/Elevation
-----------------
Az: 125.3d
El: 45.2d
Move: RIGHT

Sel: LEFT
1.Nav  1|Sel  2.Back
```

| Line | Content |
|------|---------|
| 1 | Screen title: "Azimuth/Elevation" |
| 2 | Separator line |
| 3 | Azimuth position (degrees) — from ADC |
| 4 | Elevation position (degrees) — from ADC |
| 5 | Current movement direction (UP/DOWN/LEFT/RIGHT/IDLE) |
| 6 | Currently selected menu option |
| 7 | Button instructions: `1` = Btn1 short-press, `1|` = Btn1 long-press, `2` = Btn2 short-press |

**UP/DOWN Arrows**: When elevation is moving, an arrow appears next to the EL value indicating direction. In ELF mode, arrows reflect **effective** satellite direction, not physical motor direction. Arrows hide completely when idle.

**ELF Indicator**: When ELF mode is enabled, "ELF" is displayed right-aligned next to "Sel" on the rotator settings screen.

**RX/TX Overlay**: When protocol commands are received from tracking software, they are briefly displayed on the screen. During menu interaction, a 5-second suppression timer prevents the overlay from overwriting the menu. The timer resets on any button press.

### 6.4 Rotator Settings Screen

Accessed by cycling to "Rotator" on the main screen and long-pressing Btn2.

```
Rotator Settings
----------------
Protocol: GS-232B
Hamlib Server: No
Show Commands: Yes
Rotator Variant: N360
ELF: Off

1.Nav  1|Sel  2.Back
```

| Option | Function |
|--------|----------|
| **Protocol** | Cycle through GS-232A, GS-232B, Easycom |
| **Hamlib Server** | Toggle Yes/No — determines TCP protocol (Yes=rotctl, No=configured protocol) |
| **Show Commands** | Toggle Yes/No — show/hide RX/TX overlay on OLED |
| **Rotator Variant** | Select N360, S360, N450, or S450 — opens blocking selection screen |
| **ELF** | Toggle On/Off — enables/disables over-the-top tracking mode. Opens "Elevate To?" prompt on toggle. |

**Button behavior**: Btn1 short-press cycles through options. Btn1 long-press or Btn2 short-press selects the current option. Btn2 long-press goes back.

### 6.5 WiFi Settings Screen

Accessed by cycling to "WiFi" on the main screen and long-pressing Btn2.

```
WiFi Station Settings
---------------------
Set via HTTP
Connect

SSID: RaslouwTP
IP: 192.168.x.x

1.Nav  1|Sel  2.Back
```

| Option | Function |
|--------|----------|
| **Set via HTTP** | Opens captive portal for WiFi configuration via web browser |
| **Connect/Disconnect** | Toggle WiFi connection to saved SSID |

### 6.6 Calibration Screens

Accessed by cycling to "Calibration" on the main screen and long-pressing Btn2.

#### Heading Calibration

```
CAL: HEADING
-------------
Az: 0.8V  EL: 0.3V

Btn2:Start
Btn2+:Reverse
```

| State | Display |
|-------|---------|
| Ready | "Btn2:Start" / "Btn2+:Reverse" |
| Scanning | "Dir: RIGHT" / "Scan: 5s left" / both voltages |
| Voltage High | "VOLTAGE HIGH!" / "Turn AZ pot CCW" |
| Voltage Low | "VOLTAGE LOW" / "Turn AZ pot CW" |
| Complete | "CAL COMPLETE" / "V: OK" / "Btn2:Continue" / "Btn1:Exit" |

#### Elevation Interval Calibration

Similar to heading calibration but for the elevation axis.

### 6.7 ELF Elevation Prompt

Appears when ELF mode is toggled (enabled or disabled).

```
Elevate To?
-----------
Horizon (0) [5]
45
90
Skip

1.Nav  1|Sel  2.Back
```

| Option | Effect |
|--------|--------|
| **Horizon (0)** | Sets effective elevation to 0° (default, auto-selected after 5s) |
| **45** | Sets effective elevation to 45° |
| **90** | Sets effective elevation to 90° |
| **Skip** | Leaves elevation unchanged |

**Controls**:
- Btn1 short-press: Cycle through options (resets 5s timeout)
- Btn1 long-press: Select current option
- Btn2: Back (skip, leave elevation unchanged)
- No action for 5 seconds: Auto-selects Horizon (0)

**Countdown indicator**: `[x]` next to the first option shows remaining seconds before auto-selection. Disappears immediately when any button is pressed.

---

## 7. Construction Guide

### 7.1 Parts List

| # | Item | Qty | Price (ZAR) | Supplier/SKU |
|---|------|-----|-------------|--------------|
| 1 | BMT ESP8266 NodeMCU WiFi Board [V2-ESP8266 Module ESP-12E NodeMCU Lua (V2) CP2102 USB Interface] | 1 | R100 | Communica SKU: BMT ESP8266 NODEMCU WIFI BOARD |
| 2 | HKD ESP8266 NodeMCU Driver Board [WiFi NodeMcu Lua L293D Driver Board for ESP8266 ESP-12E] | 1 | R69 | Communica SKU: HKD ESP8266 NODEMCU DRIVER BOARD |
| 3 | BDD I2C 0.96" OLED 128×64-Blue [0.96IN I2C IIC OLED 128×64 Blue. 3.3-5V 160°] | 1 | R62 | Communica SKU: BDD I2C 0.96IN OLED 128X64-BLUE |
| 4 | BMT 16-bit A/D (ADC) Dev Board [New ADS1115 16 Bit A/D Module with Internal PGA-I2C. 2-5.5V VCC] | 1 | R49 | Communica SKU: BMT 16 BIT A/D (ADC) DEV BOARD |
| 5 | XS82 Straight Inline DIN Plug (Male) • 8 way • Mini | 1 | R27 | Communica SKU: XS82 |
| 6 | PB11C02 RED Miniature Push Button Switch • Momentary • Form: SPST-0-(1) • 0.3A-50VDC • Solder-Lug • Red-Button • Round Actuator • PTM | 2 | R20 | Communica SKU: PB11C02 RED |
| 7 | BT2B NO RIBS [Enclosure ABS 130×70×44 "S20" Black No Ribs] | 1 | R49 | Communica SKU: BT2B NO RIBS |
| 8 | BMT Ribbon Jumper 40W F/F 15cm [Jumper Female/Female 15cm in 40Way Colour Cable] | 1 | R23 | Communica SKU: BMT RIBBON JUMPER 40W F/F 15CM |
| | | | | |
| | **Subtotal (required parts)** | | **R399** | |
| | | | | |
| 9 | PDC-5 Spacer Snap Fit Top Flat Base HT=5mm *(optional, for mounting)* | 9 | R1.27 each | Communica SKU: PDC-5 |
| 10 | CAB04PR,22MSCR Mylar Screened Cable 4 pair • 0.22mm² *(optional, for Yaesu DIN connection)* | 1 m | R13.80/m | Communica SKU: CAB04PR,22MSCR |
| | | | | |
| | **Estimated total (with optional items)** | | **~R425** | |

**Notes on optional items**:

- **PDC-5 spacers**: These plastic spacers are used to mount the NodeMCU Driver Board and the ADS1115 board inside the enclosure. The plastic posts need to be trimmed slightly to fit through the small mounting holes. They snap through the board on the trimmed side, and the flat base can be glued directly into the enclosure or onto another spacer. This approach lets you lift the internal boards out of the box for servicing without permanently fixing them. Alternative mounting methods (double-sided tape, hot glue, etc.) work equally well.
- **CAB04PR,22MSCR cable**: This 4-pair screened cable is suitable for the 8-core Yaesu DIN-8 connection. Any quality 8-core screened cable with appropriate gauge will work.

### 7.2 Tools Required

- Drill and drill bits (various sizes)
- File (for smoothing drilled holes)
- Soldering iron and solder
- Wire strippers
- Screwdriver (for terminal blocks)
- Multimeter (for voltage verification)

### 7.3 Box Preparation

1. Select a plastic enclosure large enough to hold the motor shield with the ESP8266 plugged in, plus the OLED display.
2. Mark a rectangle on one face of the enclosure for the OLED display, ensuring it will be visible from the front.
3. Drill out the marked rectangle and file the edges smooth.
4. Drill two holes for the push buttons.
5. Drill one hole for the 8-core screened cable (Yaesu DIN connection).
6. File a small slit near the top of the lid for the micro-USB cable, or drill a hole and cut/resolder the USB plug.

### 7.4 Mounting the OLED

1. Insert the OLED display from inside the enclosure so the screen faces outward through the cutout.
2. Secure internally using insulation tape and/or black duct tape.
3. Ensure the display is firmly held and the screen is flush with the enclosure face.

### 7.5 Connecting the Motor Shield and ESP8266 with Ribbon Cable

The 40-way female-to-female ribbon jumper cable connects the ESP8266 NodeMCU to the motor shield's GPIO headers. This provides all the control signals needed for motor control, buttons, and I2C communication.

**Connection orientation**:
- Plug one end of the ribbon cable into the motor shield's GPIO header (the longer row of pins).
- Plug the other end into the ESP8266 NodeMCU's GPIO header.
- Ensure the red stripe on the ribbon cable aligns with Pin 1 (GND) on both boards.

**Pin mapping** (ribbon cable carries these signals from motor shield to ESP8266):

| Motor Shield Header | ESP8266 Pin | Function |
|---------------------|-------------|----------|
| D1 | D1 (GPIO5) | Motor A Enable (Azimuth) |
| D2 | D2 (GPIO4) | Motor B Enable (Elevation) |
| D3 | D3 (GPIO0) | Motor A Direction (Azimuth CW/CCW) |
| D4 | D4 (GPIO2) | Motor B Direction (Elevation Up/Down) |
| D5 | D5 (GPIO14) | Button 2 (active LOW) |
| D6 | D6 (GPIO12) | I2C SDA |
| D7 | D7 (GPIO13) | I2C SCL |
| D8 | D8 (GPIO15) | Button 1 (active HIGH) |

**Important**: The ribbon cable is a straight-through connection — pin 1 to pin 1, pin 2 to pin 2, etc. No special wiring or cutting is required. Just ensure correct orientation (red stripe to GND on both ends).

### 7.6 Wiring the Buttons

**Btn1 (D8 / GPIO15)**:
- Connect one terminal to D8 on the motor shield.
- Connect the other terminal to any 3.3V rail pin on the motor shield.
- This button is active HIGH — when pressed, D8 goes from 0V to 3.3V.

**Btn2 (D5 / GPIO14)**:
- Connect one terminal to D5 on the motor shield.
- Connect the other terminal to any GND rail pin on the motor shield.
- This button is active LOW — when pressed, D5 goes from 3.3V to 0V.

**Why the difference?** GPIO15 must be LOW at reset for the ESP8266 to boot normally. The motor shield comes with a fixed pull-down resistor on this pin. If you wired Btn1 to GND instead of 3.3V, pressing it would hold GPIO15 LOW and prevent booting. GPIO14 has a programmable pull-up resistor used in firmware.

### 7.7 Wiring the I2C Bus

**OLED Display** (pins from left to right when viewing from front):
- Pin 1 (GND) → Any GND rail pin
- Pin 2 (VCC) → Any 3.3V rail pin
- Pin 3 (SCL) → D7 (GPIO13) on motor shield, using one end of a 3-leg splitter
- Pin 4 (SDA) → D6 (GPIO12) on motor shield, using one end of a 3-leg splitter

**ADS1115 Module**:
- GND → Same GND rail as OLED (or another GND pin)
- VCC → Same 3.3V rail as OLED (or another 3.3V pin)
- SCL → Third leg of the SCL splitter wire from OLED
- SDA → Third leg of the SDA splitter wire from OLED

Solder the connections to the ADS1115 module for mechanical security rather than using jumper blocks.

### 7.8 Wiring the Yaesu DIN-8 Connector

Map out the wires within the 8-core screened cable connected to the DIN plug. Connect as follows:

| Yaesu DIN Pin | Function | Motor Shield Terminal |
|---------------|----------|----------------------|
| Pin 1 | Elevation feedback voltage | ADS1115 A1 |
| Pin 2 | Azimuth CW | A+ |
| Pin 3 | Elevation Down | B+ |
| Pin 4 | Azimuth CCW | A- |
| Pin 5 | Elevation Up | B- |
| Pin 6 | Azimuth feedback voltage | ADS1115 A0 |
| Pin 7 | +12V from Yaesu controller | VM |
| Pin 8 | Common ground | GND |

Tighten all terminal screws securely.

### 7.8 Power Connections

**VIN (Logic Power)**:
- Powered via USB (5V) from a computer, USB charger, or external 5V supply.
- The on-board voltage regulator can handle up to 10V. Higher voltages will damage the unit.
- The module can draw up to ~400mA during WiFi transmission.

**VM (Motor Power)**:
- Supplied by the Yaesu controller's +12V output (Pin 7 of DIN-8).
- Used by the motor shield to drive the Yaesu relay inputs.

**CRITICAL WARNING**:
- **Do NOT install the jumper that shorts VIN and VM** on the motor shield.
- VIN runs at 5V (from USB), VM runs at 12V (from Yaesu controller).
- Shorting them would feed 12V into the USB socket and destroy the ESP8266 module.

### 7.9 Potentiometer Calibration (Before First Power-Up)

**IMPORTANT**: The Yaesu controller outputs ~4.5V by default. The ADS1115 can only handle up to 3.3V safely. You MUST adjust the trim pots on the Yaesu controller before connecting position feedback voltages to the ADS1115.

1. Locate the two trim pots on the Yaesu controller (adjacent to the meter calibration pots).
2. Rotate the azimuth rotator clockwise to the limit (maximum meter deflection).
3. Measure the voltage between Pin 6 and Ground at the DIN connector.
4. Adjust the **Azimuth** trim pot until you read exactly 3.3V.
5. Rotate the azimuth rotator counter-clockwise to the limit. Verify the voltage approaches 0V.
6. Rotate the elevation rotator UP to the limit (maximum meter deflection).
7. Measure the voltage between Pin 1 and Ground at the DIN connector.
8. Adjust the **Elevation** trim pot until you read exactly 3.3V.
9. Rotate the elevation rotator DOWN to the limit. Verify the voltage approaches 0V.

Only after both pots are calibrated should you connect the feedback voltages to the ADS1115 and power up AZELCtrl.

### 7.10 Firmware Installation

1. Install the Arduino IDE or arduino-cli on your computer.
2. Install the ESP8266 board package.
3. Install required libraries: SSD1306 OLED driver, ADS1115 driver, EEPROM library.
4. Open the AZELCtrl firmware sketch.
5. Configure the default WiFi SSID and password in the source code.
6. Select board: "NodeMCU 1.0 (ESP-12E Module)" or equivalent.
7. Select port: Your ESP8266's serial port.
8. Upload the firmware.
9. Power up the unit and verify the OLED displays the startup sequence.

---

## 8. Operating the Device

### 8.1 Power-Up Sequence

1. Connect power via USB (or VIN terminal).
2. OLED displays "WiFi Connecting" with SSID and elapsed time.
3. On success: "WiFi Connected!" with IP address. Press any button or wait 30 seconds.
4. On failure: "WiFi FAILED!" with status code. Press any button or wait 30 seconds.
5. OLED switches to the Az/El control screen.

### 8.2 Manual Control via Buttons

1. Press **Btn1** repeatedly to cycle through the menu: Idle → Left → Right → Up → Down → AZ POT → EL POT → Rotator → WiFi → Calibration → Idle.
2. Press and **hold Btn2** to move in the selected direction. Release to stop.
3. For menu options (Rotator, WiFi, Calibration), **long-press Btn2** to enter the submenu.

### 8.3 Remote Control via Web Interface

1. Connect a device to the same WiFi network as AZELCtrl.
2. Open a web browser and navigate to the IP address shown on the OLED.
3. Use the directional buttons (UP/LEFT/RIGHT/DOWN) — press and hold to move.
4. Press STOP to halt all movement.
5. Positions and status update automatically every 300ms.

### 8.4 Connecting Tracking Software

#### Gpredict

1. Open Gpredict preferences → Satellite → Control → Rotator control.
2. Set type to "Hamlib rotctld".
3. Set host to AZELCtrl's IP address.
4. Set port to 4533.
5. **Critical**: Ensure AZELCtrl's **Hamlib Server** setting is **Yes** (under Rotator Settings in the OLED menu). Without this, AZELCtrl will speak GS-232/Easycom over TCP instead of the rotctl protocol that Gpredict expects, and the connection will fail.
6. Enable tracking for the desired satellite.

**Note on ELF (over-the-top) mode**: Gpredict implements the elevation-flip technique natively. When a satellite pass crosses the rotator's wrap-around boundary, Gpredict automatically transforms the coordinates and sends the appropriate physical targets. **Do not enable ELF mode on AZELCtrl when using Gpredict** — doing so would apply the transformation twice, resulting in incorrect antenna pointing.

#### Rotor

1. Configure a new rotator connection.
2. Set type to GS-232B (or your configured protocol).
3. Set connection to TCP/IP.
4. Set host to AZELCtrl's IP address.
5. Set port to 4533.
6. Ensure AZELCtrl's Hamlib Server setting is **No**.
7. Test the connection.

### 8.5 Configuring via OLED Menus

- **Protocol**: Cycle to "Rotator" → long-press Btn2 → select "Protocol" → cycle through GS-232A/GS-232B/Easycom → confirm.
- **Hamlib Server**: Same path → select "Hamlib Server" → toggle Yes/No.
- **Rotator Variant**: Same path → select "Rotator Variant" → choose N360/S360/N450/S450.
- **ELF Mode**: Same path → select "ELF" → toggle On/Off → respond to "Elevate To?" prompt.

---

## 9. Troubleshooting

### 9.1 OLED Shows Nothing

| Possible Cause | Solution |
|---------------|----------|
| I2C connection loose | Check SDA=D6 and SCL=D7 connections |
| OLED not powered | Verify 3.3V and GND connections |
| Wrong I2C address | OLED should be at address 0x3C |
| OLED mounted incorrectly | Ensure screen faces outward and pins are properly seated |

### 9.2 Motors Not Moving

| Possible Cause | Solution |
|---------------|----------|
| Motor enable pins not connected | Verify D1 (AZ enable) and D2 (EL enable) on shield |
| Motor shield not powered | Check VM connection to Yaesu +12V |
| Direction-reverse pause active | Wait for safety pause countdown to complete |
| Yaesu controller not powered | Verify Yaesu controller is on and receiving power |
| DIN connector loose | Check all terminal screws are tight |

### 9.3 ADS1115 Not Found

| Possible Cause | Solution |
|---------------|----------|
| I2C bus conflict | Ensure no devices share conflicting addresses |
| Wiring issue | Check A0 and A1 connections to Yaesu feedback pins |
| Wrong I2C address | ADS1115 default address is 0x48; check address jumpers |
| Voltage too high | Verify Yaesu trim pots are adjusted to ≤3.3V |

### 9.4 WiFi Connectivity Issues

| Possible Cause | Solution |
|---------------|----------|
| Wrong SSID/password | Reconfigure via captive portal (select "Set via HTTP") |
| Signal too weak | Move AZELCtrl closer to access point |
| IP address conflict | Check router DHCP lease table |
| ESP8266 not connecting | Check OLED for error status code |

### 9.5 Position Readings Incorrect

| Possible Cause | Solution |
|---------------|----------|
| Potentiometers not calibrated | Run calibration procedure in OLED menu |
| Wrong rotator model selected | Verify Rotator Variant matches your hardware |
| Trim pots drifted | Re-adjust Yaesu controller trim pots |
| Loose wiring | Check ADS1115 connections to Yaesu feedback pins |

### 9.6 Tracking Software Connection Refused

| Possible Cause | Solution |
|---------------|----------|
| AZELCtrl not connected to WiFi | Check OLED for WiFi status |
| Wrong port number | AZELCtrl listens on port 4533 |
| Wrong protocol | Match Hamlib Server setting to software expectations |
| Firewall blocking | Check network firewall rules |

---

## 10. Appendices

### 10.1 Yaesu DIN-8 Pinout Reference

| Pin | Name | Function | Signal Type |
|-----|------|----------|-------------|
| 1 | EL Feedback | Elevation position voltage | Analog output (0–3.3V after calibration) |
| 2 | AZ CW | Azimuth clockwise control | Digital input (ground to activate) |
| 3 | EL Down | Elevation down control | Digital input (ground to activate) |
| 4 | AZ CCW | Azimuth counter-clockwise control | Digital input (ground to activate) |
| 5 | EL Up | Elevation up control | Digital input (ground to activate) |
| 6 | AZ Feedback | Azimuth position voltage | Analog output (0–3.3V after calibration) |
| 7 | +13.5V Out | Power output from controller | DC 12–14V (used for VM) |
| 8 | GND | Common ground | Ground reference |

### 10.2 ESP8266 Pin Mapping Table

| NodeMCU Label | GPIO | Function in AZELCtrl |
|---------------|------|---------------------|
| D0 | GPIO16 | Unused (tricky for buttons) |
| D1 | GPIO5 | Motor A Enable (Azimuth) |
| D2 | GPIO4 | Motor B Enable (Elevation) |
| D3 | GPIO0 | Motor A Direction (Azimuth CW/CCW) |
| D4 | GPIO2 | Motor B Direction (Elevation Up/Down) |
| D5 | GPIO14 | Button 2 (active LOW) |
| D6 | GPIO12 | I2C SDA |
| D7 | GPIO13 | I2C SCL |
| D8 | GPIO15 | Button 1 (active HIGH) |
| TX | GPIO1 | USB Serial TX |
| RX | GPIO3 | USB Serial RX |
| A0 | ADC0 | Analog input (not used) |

### 10.3 Protocol Command Reference

#### GS-232B Commands

| Command | Description | Example | Response |
|---------|-------------|---------|----------|
| `C` | Query position | `C\r` | `C125.45\r` (Az=125°, El=45°) |
| `Maaa` | Move azimuth | `M180\r` | `OK\r` |
| `Waaa eee` | Move to position | `W180 045\r` | `OK\r` |
| `L` | Azimuth left | `L\r` | `OK\r` |
| `R` | Azimuth right | `R\r` | `OK\r` |
| `U` | Elevation up | `U\r` | `OK\r` |
| `D` | Elevation down | `D\r` | `OK\r` |
| `S` | Stop | `S\r` | `OK\r` |

#### rotctl Commands

| Command | Description | Example | Response |
|---------|-------------|---------|----------|
| `p` | Query position | `p\r\n` | `125\n45\nRPRT 0\n` |
| `P az el` | Move to position | `P 180 45\r\n` | `RPRT 0\n` |
| `S` | Stop | `S\r\n` | `RPRT 0\n` |

### 10.4 EEPROM Layout

| Bytes | Size | Content | Magic Byte |
|-------|------|---------|------------|
| 0–66 | 67 | WiFi SSID and password | 0xAA at byte 0 |
| 67–83 | 17 | Calibration min/max voltages | 0xCA at byte 67 |
| 84–85 | 2 | WiFi mode (STA/AP/Disabled) | — |
| 86–87 | 2 | Network configuration | — |
| 88–89 | 2 | Rotator model (N360/S360/N450/S450) | — |
| 90 | 1 | ELF mode enabled/disabled | — |

---

*End of Document*

*AZELCtrl — Built for amateur satellite operators, by amateur satellite operators.*

*73 ZS6TW*
