# AZELCtrl — Complete User Manual

**Version 1.5.3**  
**Author: ZS6TW**  
**Date: August 2026**

---

## Table of Contents

1. [Introduction](#1-introduction)
   - [What is AZELCtrl?](#11-what-is-azelctrl)
   - [The Problem It Solves](#12-the-problem-it-solves)
   - [Target Audience](#13-target-audience)
2. [Key Features & Benefits](#2-key-features--benefits)
3. [How It Works](#3-how-it-works-high-level-overview)
4. [Technical Details](#4-technical-details)
5. [Over-the-Top (ELF) Tracking Mode](#5-over-the-top-elf-tracking-mode)
6. [Menu System Reference](#6-menu-system-reference)
7. [Construction Guide](#7-construction-guide)
8. [Operating the Device](#8-operating-the-device)
   - [Power-Up Sequence](#81-power-up-sequence)
   - [Manual Control via Buttons](#82-manual-control-via-buttons)
   - [Remote Control via Web Interface](#83-remote-control-via-web-interface)
   - [Connecting Tracking Software](#84-connecting-tracking-software)
   - [Configuring via OLED Menus](#85-configuring-via-oled-menus)
9. [Troubleshooting](#9-troubleshooting)
10. [Appendices](#10-appendices)

---

## 1. Introduction

### 1.1 What is AZELCtrl?

AZELCtrl is a WiFi-enabled, dual-axis rotator controller designed for amateur satellite operators using Yaesu elevation-azimuth rotator systems. Built around an ESP8266 microcontroller, it provides precise motor control, real-time position feedback, and network connectivity for integration with popular satellite tracking software such as Gpredict and Rotor.

### 1.2 The Problem It Solves

Yaesu rotator controllers (e.g., G-5600B) provide excellent mechanical performance but lack modern connectivity options. Traditional control requires direct serial connection to a computer, limiting flexibility and remote operation. AZELCtrl bridges this gap by:

- Providing both USB serial and WiFi-based remote control via web browser and TCP/IP protocols
- Enabling seamless integration with satellite tracking software running locally via USB or remotely on the network
- Adding intelligent features such as over-the-top tracking mode for problematic satellite passes
- Offering real-time position monitoring via an integrated OLED display

### 1.3 Target Audience

This product is designed for amateur radio operators who:

- Own a Yaesu azimuth-elevation rotator system (G-550, G-550X, G-5600, G-5600B, or compatible)
- Track low Earth orbit (LEO) satellites and require automated antenna pointing
- Want flexible control options (USB serial for simple setups, WiFi for remote operation) without replacing existing rotator hardware
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

- **Flexible connectivity** — control via USB serial (simple, direct) or TCP/IP network (remote, flexible)
- **Remote monitoring** — check antenna position from anywhere via web browser when using WiFi mode
- **Multiple control methods** — physical buttons, web interface, USB serial, or TCP/IP network
- **Intelligent tracking** — ELF mode automatically handles passes that would otherwise cause the rotator to perform a damaging full rotation
- **Safety mechanisms** — voltage monitoring protects sensitive electronics; direction-reverse pause prevents motor damage
- **Compact and self-contained** — all components fit in a small enclosure

---

## 3. How It Works (High-Level Overview)

### 3.1 System Architecture

AZELCtrl sits between your satellite tracking software and your Yaesu rotator controller. All connections between AZELCtrl and the Yaesu controller go through a single 8-wire cable terminating on a DIN-8 connector:

**Option 1: USB Serial Connection (Simpler, Direct)**
```
                                                            ┌────────────────────────────────┐
                                                            │   PC, Mac, Pi with             │
                                                            │   Satellite Tracking Software  │
                                                            │   (Gpredict, Rotor,etc)        │
                                                            └─────────┬──────────────────────┘
                                                                      │        USB Serial
                                                            USB cable │ (virtual COM port, 5V power)
                                                                      │
                                            ┌─────────────────────────│─────────────────────────────────────────────┐
                                            │ AZELCtrl                │                                             │
                                            │                         ▼                                             │
                                            │                ┌──────────────────┐                                   │
                                            │                │ ESP8266 (NodeMCU)│                                   │
                                            │                └────────┬─────────┘                                   │
                                            │                         │ Power + GPIOs                               │
                                            │                         ▼                                             │
                                            │               ┌──────────────────┐    Motor VCC (12-14V)              │
                                            │               │   Motor Shield   │◄────────────────────────┐          │     
                                            │    ┌──────────┤    (L293DD)      │────────────────────────►│          │ 
                                            │    │          └──┬───────────────┘    L, R, U, D, GND      │          │
                                            │    │             │                                         │          │ 
                                            │    │             │ I2C bus                                 │          │
                                            │    │             │ (SDA/SCL)                               │          │
                                            │    │        ┌────┴────┐                                    │          │  
                                            │    │        │         │                                    │          │  
                                            │    │        ▼         ▼                                    │          │   
                                            │    │   ┌────────┐ ┌────────┐                               │          │
                                            │    │   │ OLED   │ │ ADS1115│       Az, El Voltages         │          │
                                            │    │   │Display │ │ ADC    │◄──────────────────────────────┤          │
                                            │    │   │(128×64)│ │        │                               │          │
                                            │    │   └────────┘ └────────┘                               │          │
                                            │    │       ▲          ▲                                    │          │
                                            │    └───────┴──────────┘                  Yaesu DIN-8 Cable │          │
                                            │       Power (3.3V) & GND                                   │          │
                                            └────────────────────────────────────────────────────────────│──────────┘
                                                                                                         │
                                                                                                         ▼
                                                        ┌─────────────────┐                    ┌───────────────────┐      
                                                        │   Rotator       │                    │ Yaesu Rotator     │
                                                        │  (antenna)      │   ◄──────────►     │ Controller        │      
                                                        │                 │                    │ (G-5600B etc.)    │      
                                                        └─────────────────┘                    └───────────────────┘          
```

**Option 2: WiFi/TCP Connection (Remote, Flexible)**
```
                                            ┌───────────────────────────────┐     WiFi      ┌────────────┐
                                            │   PC, Mac, Pi, Phone with     │ ◄───────────► │            │
                                            │   Satellite Tracking Software │    TCP/IP     │  WiFi AP   │
                                            │   (Gpredict, Rotor)  OR       │               │            │
                                            │   AZELCtrl In-Browser Tacking │               │            │
                                            └───────────────────────────────┘               └────────────┘
                                                                                                  ▲ 
                                                                                                  │ WiFi TCP/IP             
                                                            ┌──────────────────────┐              │
                                                            │   USB Power Supply   │              │
                                                            └─────────┬────────────┘              │
                                                                      │                           │
                                                            USB cable │ (5V power only)           │
                                                                      │                           │
                                            ┌─────────────────────────│───────────────────────────│─────────────────┐
                                            │ AZELCtrl                │                           │                 │
                                            │                         ▼                           │                 │
                                            │                ┌─────────────────────────┐          │                 │
                                            │                │ ESP8266 (NodeMCU)| Wifi │ ◄────────┘                 │
                                            │                └────────┬────────────────┘                            │
                                            │                         │ Power + GPIOs                               │
                                            │                         ▼                                             │
                                            │               ┌──────────────────┐    Motor VCC (12-14V)              │
                                            │               │   Motor Shield   │◄────────────────────────┐          │     
                                            │    ┌──────────┤    (L293DD)      │────────────────────────►│          │ 
                                            │    │          └──┬───────────────┘    L, R, U, D, GND      │          │
                                            │    │             │                                         │          │ 
                                            │    │             │ I2C bus                                 │          │
                                            │    │             │ (SDA/SCL)                               │          │
                                            │    │        ┌────┴────┐                                    │          │  
                                            │    │        │         │                                    │          │  
                                            │    │        ▼         ▼                                    │          │   
                                            │    │   ┌────────┐ ┌────────┐                               │          │
                                            │    │   │ OLED   │ │ ADS1115│       Az, El Voltages         │          │
                                            │    │   │Display │ │ ADC    │◄──────────────────────────────┤          │
                                            │    │   │(128×64)│ │        │                               │          │
                                            │    │   └────────┘ └────────┘                               │          │
                                            │    │       ▲          ▲                                    │          │
                                            │    └───────┴──────────┘                  Yaesu DIN-8 Cable │          │
                                            │       Power (3.3V) & GND                                   │          │
                                            └────────────────────────────────────────────────────────────│──────────┘
                                                                                                         │
                                                                                                         ▼
                                                        ┌─────────────────┐                    ┌───────────────────┐      
                                                        │   Rotator       │                    │ Yaesu Rotator     │
                                                        │  (antenna)      │   ◄──────────►     │ Controller        │      
                                                        │                 │                    │ (G-5600B etc.)    │      
                                                        └─────────────────┘                    └───────────────────┘     
          

```

**The single Yaesu DIN-8 cable carries all signals:**

| DIN Pin | Signal | Connection in AZELCtrl |
|---------|--------|------------------------|
| Pin 1 | Elevation feedback voltage | ADS1115 A1 |
| Pin 2 | Azimuth CW motor control | Motor Shield A+ |
| Pin 3 | Elevation Down motor control | Motor Shield B+ |
| Pin 4 | Azimuth CCW motor control | Motor Shield A- |
| Pin 5 | Elevation Up motor control | Motor Shield B- |
| Pin 6 | Azimuth feedback voltage | ADS1115 A0 |
| Pin 7 | +12V power (from Yaesu) | Motor Shield VM |
| Pin 8 | Ground | Motor Shield GND |

There are no separate cables for motor control or position sensing — everything goes through this one DIN-8 connector.

**Which option should you use?**

- **USB Serial** is simpler: one cable provides both power and data. No WiFi configuration needed. Connect AZELCtrl to your computer via USB, select the virtual COM port in your tracking software, and you're ready. This is ideal if your computer is close to the rotator setup.

- **WiFi/TCP** is more flexible: your tracking software can run on any device on the same network — a laptop across the room, a tablet in your shack, or even a phone. AZELCtrl can be powered by a standalone USB power supply (no computer needed). This is ideal for remote operation or when your computer isn't near the rotator.

### 3.2 Data Flow

AZELCtrl supports two communication interfaces:

**USB Serial Interface**:
1. **Command Path**: Satellite tracking software sends position commands (azimuth/elevation) over USB serial to AZELCtrl's virtual COM port. The ESP8266 interprets the command according to the configured protocol (GS-232A, GS-232B, or Easycom), calculates the required motor movements, and sends control signals to the motor shield.
2. **Position Feedback**: AZELCtrl reports current position back over USB serial when polled.

**TCP/IP Network Interface**:
1. **Command Path**: Satellite tracking software sends position commands over TCP to AZELCtrl on port 4533. The protocol spoken is determined by the Hamlib Server setting (Yes = rotctl text protocol, No = configured GS-232/Easycom protocol).
2. **Position Feedback**: AZELCtrl reports current position back over TCP when polled.

**Motor Control Path** (same for both interfaces):
The motor shield (L293DD) receives direction and enable signals from the ESP8266 and switches the Yaesu controller's relay inputs to ground, activating the appropriate rotator motors.

**Position Feedback Path** (same for both interfaces):
The Yaesu controller outputs analog voltages proportional to the current azimuth and elevation positions. These voltages are read by the ADS1115 ADC module, converted to digital values by the ESP8266, and displayed on the OLED screen. Position data is also reported back to the tracking software when polled, via whichever interface is in use.

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

### 4.5 Communication Interfaces and Protocol Stack

AZELCtrl supports two communication interfaces:

#### USB Serial Interface

When AZELCtrl is connected to a computer via USB, it appears as a **virtual COM port** (e.g., COM3 on Windows, /dev/cu.usbserial-* on macOS, /dev/ttyUSB* on Linux). The ESP8266's built-in USB-to-serial converter (CP2102) handles the conversion transparently.

- **Protocol**: The configured serial protocol is used (GS-232A, GS-232B, or Easycom — selected in the PC Interface submenu under SETUP).
- **Baud rate**: 115200 baud (default).
- **Power**: The USB connection provides 5V power to AZELCtrl, so no separate power supply is needed.

This interface is ideal when:
- Your computer is physically close to the rotator setup.
- Your tracking software only supports serial connections (some older or simpler programs).
- You want the simplest possible setup with no WiFi configuration.
- Your tracking software has issues with TCP/IP but works reliably over serial.

#### TCP/IP Network Interface

AZELCtrl runs a TCP server on **port 4533** that accepts connections from satellite tracking software. The protocol spoken over TCP is determined by the **Hamlib Server** setting in the rotator menu:

| Hamlib Server | TCP Protocol |
|---------------|-------------|
| **Yes** | rotctl text protocol (`p`, `P`, `S` commands) |
| **No** | Configured serial protocol (GS-232A, GS-232B, or Easycom) |

The server always listens on port 4533 regardless of the Hamlib Server setting — the setting only determines which protocol dialect is used for communication.

This interface is ideal when:
- Your tracking software runs on a different device (laptop, tablet) on the same network.
- You want remote operation without a physical cable to the rotator.
- AZELCtrl is powered by a standalone USB power supply (no computer connection needed).

**Important**: Both interfaces can be used simultaneously. AZELCtrl processes commands from whichever interface they arrive on. However, having two programs sending commands at the same time (one over USB, one over TCP) will cause conflicts — only use one control source at a time.

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
| **N360** | 0°–360° (North-centered) | 0°/360° (North) | Southern Hemisphere |
| **S360** | 0°–360° (South-centered) | 180° (South) | Northern Hemisphere |
| **N450** | 0°–450° (North-centered) | 0°/360° (North) | Southern Hemisphere, extended range |
| **S450** | 0°–450° (South-centered) | 180° (South) | Northern Hemisphere, extended range |

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
| 0–66 | 67 | WiFi SSID (data at 1–32) and password (data at 34–65) | 0xAA at byte 66 |
| 67–83 | 17 | Calibration min/max voltages (4× float: Az min, Az max, El min, El max) | 0xCA at byte 83 |
| 84–85 | 2 | WiFi mode (STA/AP/Disabled) | 0x4D at byte 85 |
| 86–87 | 2 | Network settings (Hamlib Server toggle) | 0x4E at byte 87 |
| 88–89 | 2 | Rotator model (N360/S360/N450/S450) | 0x5A at byte 89 |
| 90 | 1 | ELF mode enabled/disabled | — |
| 91–99 | 9 | AP password (8 chars) | 0x41 ('A') at byte 99 |
| 100–104 | 5 | Protocol settings — baud index, show commands, reverse delay, protocol variant | 0xB7 at byte 102 |

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

Consider a Southern Hemisphere operator using a north-centered rotator. A satellite rises in the southwest at Az=225°, climbs to a peak near Az=180° (due south), and sets in the southeast at Az=135°. The rotator tracks perfectly until the satellite approaches 180°. To continue tracking, the controller would need to move from, say, Az=181° to Az=179° — but the rotator's mechanical range ends at ±180°. To reach 179°, the drive motor must reverse direction and travel nearly 360° clockwise (past north, east, south, and back). On a typical rotator with a slew speed of around 4 degrees per second, this journey takes well over a minute.

During that minute, the antenna is pointed nowhere near the satellite. For a voice or data contact, this is catastrophic. The link drops, the pass is lost, and the opportunity is wasted.

The equivalent problem for Northern Hemisphere operators occurs when a satellite passes to their north — rising at Az=330° and setting at Az=90°, crossing through Az=0°/360°. The mechanics are identical, and the solution is the same.

### 5.2 The Geometric Principle

Most amateur-grade elevation rotators designed for satellite work support elevation movement not just from 0° (horizon) to 90° (zenith), but all the way to 180°. An elevation of, for example, 135° means the antenna is tilted past vertical and is now pointing toward a direction 45° above the horizon on the **opposite side** of the rotator.

This mechanical capability is the key to the over-the-top technique. We refer to this as ELF (ELevation Flip) mode.

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
- **Pre-positioning before AOS**: For over-the-top passes, the antenna must be pre-positioned to the starting position before the satellite rises. This typically means the elevation will be near 180° while waiting for AOS. The firmware includes a built-in web-based tracking feature that automatically determines when ELF mode is required. When the user clicks the Start button to begin tracking the selected satellite, the system prompts the user if ELF mode needs to be enabled.
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

**Button actions in rotator control mode**:

| Action | Effect |
|--------|--------|
| Btn1 short-press | Cycle through: SETUP → LEFT → RIGHT → UP → DOWN → ELF |
| Btn2 press (hold) | Start movement in selected direction (LEFT/RIGHT/UP/DOWN). Release to stop. |
| Btn2 press (when SETUP selected) | Enter SETUP menu |
| Btn2 press (when ELF selected) | Toggle ELF mode On/Off, shows "Elevate To?" prompt |
| Btn1+Btn2 long-press (when SETUP selected) | Reboot device |

**Button actions in menus**:

| Action | Effect |
|--------|--------|
| Btn1 short-press | Move cursor down one item (wraps to top) |
| Btn1 long-press | Select current item |
| Btn2 | Go back to parent menu |

**Understanding the menu footer**:

Every menu screen displays a footer line that tells you which button action to use. The format is:

| Symbol | Meaning |
|--------|---------|
| `1.` | Short-press Btn1 (tap and release quickly) |
| `1│` | Long-press Btn1 (press and hold for ~1 second) |
| `1!` | Hold Btn1 (press and continue holding until action completes) |
| `2.` | Short-press Btn2 |
| `2│` | Long-press Btn2 |
| `2!` | Hold Btn2 |

The separator between actions is a space and a period (` . `) or a pipe (` | `). For example:

- `1.Nav 1|Sel 2.Back` means: short-press Btn1 to navigate, long-press Btn1 to select, short-press Btn2 to go back
- `1.OK 2.Back` means: short-press Btn1 to confirm, short-press Btn2 to go back
- `1.Done 2.Cancel` means: short-press Btn1 to finish, short-press Btn2 to cancel

The footer adapts to the current screen's available actions.

### 6.2 Startup Screens

#### Splash Screen

On power-up, AZELCtrl displays a splash screen for 3 seconds:

```
AZELCtrl

Ver: 1.5.1
Date: Aug 1 2026
By: ZS6TW
```

Shows product name, firmware version, build date, and author.

#### Missing Settings

If mandatory settings are incomplete (rotator variant not configured, calibration not done, or WiFi mode not set), a notice appears for 3 seconds:

```
Missing
Settings!
```

After the notice, the device goes directly to the SETUP menu. The footer omits the Back button — you must complete the mandatory settings before leaving.

#### Boot Menu

If all mandatory settings are configured, the Boot menu appears:

```
Boot
----
Rotator Ctrl
Setup

· · · · · · · · · · · · · · · ·
1.Nav 1|Sel
```

- **Rotator Ctrl**: Starts the rotator controller. Has a 3-second countdown — auto-selects if no button is pressed.
- **Setup**: Opens the SETUP menu.

**Button behavior**: Btn1 short-press cycles options. Btn1 long-press selects. There is no Back button (this is the root menu).

#### WiFi Connecting

When "Rotator Ctrl" is selected and WiFi mode is set to Station, the WiFi connecting screen appears:

```
WiFi Connect

SSID: YourWifiSSID
Waiting: 3s
```

Shows the SSID being connected to and a running elapsed time counter. The device attempts to connect for up to 15 seconds.

#### WiFi Connected

On successful connection:

```
Connected!

IP: 192.168.1.42
Signal: -65 dBm
SSID: YourWifiSSID
                    3
```

Shows the assigned IP address, signal strength, and SSID. A countdown timer in the bottom-right corner counts down from 5 seconds. Press any button to skip the countdown and proceed immediately to the rotator control screen.

#### WiFi Failed

If the connection times out:

```
WiFi
Failed

SSID: YourWifiSSID

Status: WL_CONNECT_FAILED
```

Shows the SSID that failed and the WiFi status code. Displays for 2 seconds, then proceeds to the rotator control screen without WiFi. The device can still be controlled via USB serial or through the OLED menu.

### 6.3 Main Az/El Control Screen

This is the primary screen shown when the rotator controller is running. It has no title bar — the full display is used for position values.

**Normal idle state** (no movement, no protocol commands):

```
Az:  125.3d →
El:   45.2d ↑

Sel: LEFT    ELF

· · · · · · · · · · · · · · · ·
1.Cycle 2!Go
```

| Line | Content |
|------|---------|
| 1 | Azimuth position (large font) with optional movement arrow (→ for CW, ← for CCW) on the right |
| 2 | Elevation position (large font) with optional movement arrow (↑ for UP, ↓ for DOWN) on the right |
| 3 | When idle: `Sel:` followed by the currently selected direction (SETUP/LEFT/RIGHT/UP/DOWN). When ELF mode is enabled, `ELF` appears right-aligned on this line. |
| 4 | Dotted separator line above the footer |
| 5 | Button instructions footer |

**When moving** (but no protocol commands showing):

```
Az:  125.3d
El:   45.2d

Moving: RIGHT

· · · · · · · · · · · · · · · ·
1.Cycle 2!Go
```

Line 3 shows `Moving: direction` instead of `Sel:` while motors are active.

**When receiving protocol commands** (if Show Commands is enabled):

```
Az:  125.3d
El:   45.2d

· · · · · · · · · · · · · · · ·
RX: C
TX: AZ=125 EL=045
```

Lines 3-4 show the last received command (RX) and response (TX). This overlay is suppressed for 5 seconds after any button press so the menu stays visible during interaction.

**SAFETY PAUSE screen** (shown when reversing direction):

```
SAFETY PAUSE
------------
AZ axis
125.3d El: 45.2d

· · · · · · · · · · · · · · · ·
1.Cycle 2!Go
```

Title bar shows "SAFETY PAUSE" with which axis is pausing. Press Btn2 to skip the pause and continue immediately.

**TCP Connection Indicator**: When a tracking software is connected via TCP, a small white dot appears in the top-right corner of the screen.

**Footer varies by selection**:
- SETUP selected: `1.Cycle 2!Go 1+2!|RST` (1+2 long-press reboots)
- ELF selected (on): `1.Cycle 2!Off`
- ELF selected (off): `1.Cycle 2!On`
- Direction selected: `1.Cycle 2!Go`

**Movement arrows**: Arrows on the Az/El lines indicate which axis is currently moving:
- Azimuth: → (right arrow) for CW, ← (left arrow) for CCW
- Elevation: ↑ (up arrow) for UP, ↓ (down arrow) for DOWN
- In ELF mode, elevation arrows reflect **effective** satellite direction (swapped from physical motor direction)
- Arrows disappear when the axis is idle

**ELF Indicator**: When ELF mode is enabled, "ELF" is displayed right-aligned on the `Sel:` line.

### 6.4 SETUP Menu

Accessed from the Boot screen by selecting "Setup", or from the main Az/El control screen by cycling to "SETUP" and long-pressing Btn2.

```
SETUP
-----
Rot Variant (N360)
Rot Calibrate (Y)
Network
PC Interface
Wipe all settings

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

| Option | Function |
|--------|----------|
| **Rot Variant** | Opens rotator model selection screen to choose N360, S360, N450, or S450 |
| **Rot Calibrate** | Opens the Voltage Calibration menu for azimuth and elevation potentiometers |
| **Network** | Opens Network submenu (see below) |
| **PC Interface** | Opens PC Interface submenu (see below) |
| **Wipe all settings** | Resets all configuration to factory defaults (with confirmation) |

**Labels update dynamically**:
- **Rot Variant**: Shows current model in parentheses when configured (e.g., "Rot Variant (N360)"), or just "Rot Variant" if not yet set.
- **Rot Calibrate**: Shows "(Y)" if both axes are calibrated, "(N)" if either axis is uncalibrated.
- **Back button**: Hidden until all mandatory settings are configured (rotator model selected, both axes calibrated, WiFi mode set).

**Button behavior**: Btn1 short-press cycles through options. Btn1 long-press selects the current option. Btn2 goes back to the previous screen.

#### Rotator Variant Selection Screen

Selecting "Rot Variant" opens a dedicated model selection screen:

```
Rotator Variant?
----------------
N360 (-180 to 180)
S360 (0 to 360)
N450 (-225 to 225)
S450 (-45 to 405)

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

**Understanding the model names:**
- The first letter indicates the direction the rotator points when the azimuth is at the center of its range: **N** = North, **S** = South.
- The number indicates the total azimuth span the rotator is designed to handle: **360** = 360° of travel, **450** = 450° of travel (extended range with an additional 90° beyond standard).

| Model | Range | Wrap Boundary | Typical Use |
|-------|-------|---------------|-------------|
| **N360** | -180° to +180° | 0°/360° | Southern Hemisphere |
| **S360** | 0° to 360° | 180° (South) | Northern Hemisphere |
| **N450** | -225° to +225° | 0°/360° | Southern Hemisphere, extended range |
| **S450** | -45° to +405° | 180° (South) | Northern Hemisphere, extended range |

**Controls**:
- Btn1 short-press: Cycle through models (resets any auto-select timer)
- Btn1 long-press: Select and save the highlighted model
- Btn2: Go back without changing

**Important**: Changing the rotator model clears the existing calibration values. You must recalibrate after selecting a new model.

#### Voltage Calibration Menu

Selecting "Rot Calibrate" opens the calibration menu:

```
Voltage Calibration
-------------------
Azimuth
Elevation
Skip (Az=El=3.2V)

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

| Option | Function |
|--------|----------|
| **Azimuth** | Start azimuth potentiometer calibration (sweep and return sequence) |
| **Elevation** | Start elevation potentiometer calibration (sweep and return sequence) |
| **Skip (Az=El=3.2V)** | Set both axes to fixed voltage ranges (min=0V, max=3.2V) without automatic calibration |

**Controls**:
- Btn1 short-press: Cycle through options
- Btn1 long-press: Select current option
- Btn2: Go back to SETUP menu

See section 6.6 for details on the calibration screens.

#### Network Submenu

Selecting "Network" opens the network settings menu:

```
Network
-------
WiFi Mode(Station)
Hamlib Server (Y)

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

| Option | Function |
|--------|----------|
| **WiFi Mode(label)** | Opens WiFi Mode submenu to select Station, Access Point, or Disable. Label reflects current mode: `WiFi Mode(None)` (not set), `WiFi Mode(Station)`, `WiFi Mode(AP)`, or `WiFi Mode(Disabled)` |
| **Hamlib Server** | Toggle Yes/No — determines TCP protocol: Yes = rotctl text protocol, No = configured serial protocol (GS-232A, GS-232B, or Easycom). Footer shows current toggle action (e.g., `1.Nav 1|Yes 2.Back`) |

**Controls**:
- Btn1 short-press: Cycle through options
- Btn1 long-press: Select current option (or toggle Hamlib Server when on that line)
- Btn2: Go back to SETUP menu

##### WiFi Mode Submenu

Selecting the WiFi Mode line opens the WiFi Mode submenu:

```
WiFi Mode
---------
Station
Access Point
Disable

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

| Option | Function |
|--------|----------|
| **Station** | Sets WiFi mode to Station and opens the Station Settings screen |
| **Access Point** | Sets WiFi mode to Access Point and opens the AP Mode menu |
| **Disable** | Disconnects from any network, turns off WiFi, clears saved credentials, and returns to SETUP |

**Controls**:
- Btn1 short-press: Cycle through options
- Btn1 long-press: Select current option
- Btn2: Go back to Network menu

##### Station Settings Screen

Selecting "Station" opens the Station Settings screen:

```
Wifi Station Settings
---------------------
Set via HTTP
Connect

SSID: YourNetworkName
Passwd: a*..*z

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

| Option | Function |
|--------|----------|
| **Set via HTTP** | Starts a captive portal (SSID: AZELCTRL_XXXX, IP: 192.168.4.1). Connect your phone/laptop to the device network and open a browser to configure your home WiFi credentials |
| **Connect** | Connects to the saved SSID. Label changes to "Disconnect" when connected |

**Information displayed**:
- **SSID**: Shows the saved SSID, or "None" if no credentials are saved
- **IP**: Shows the assigned IP address when connected
- **Passwd**: Shows a masked password (first and last character visible) when credentials are saved but not connected

**Controls**:
- Btn1 short-press: Cycle through options
- Btn1 long-press: Select current option
- Btn2: Go back to SETUP menu

##### AP Mode Menu

Selecting "Access Point" opens the AP Mode menu:

```
AP Mode
-------
AP Status
Regen AP Password

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

| Option | Function |
|--------|----------|
| **AP Status** | Show AP information (SSID, password, IP) |
| **Regen AP Password** | Generate a new random 8-character alphanumeric password for AP access |

**Controls**:
- Btn1 short-press: Cycle through options
- Btn1 long-press: Select current option
- Btn2: Go back to WiFi Mode menu

**AP Mode**: When enabled, the device creates a WiFi access point with SSID "AZELCTRL_XXXX" where XXXX is the last 4 hex digits of the device MAC address. This allows distinguishing multiple devices. By default, the AP password is "yaesu123". You can generate a new random 8-character password by selecting "Regen AP Password". The process:
1. Screen shows "Generating new password..."
2. Press Btn1 to provide timing entropy for randomness
3. New 8-character password is displayed — note it down!
4. Press Btn1 to acknowledge and return to AP Mode menu

**Note**: WPA2 requires a minimum of 8 characters for the password.

**AP Status screen** shows:
```
AP Status
---------
AP Mode Active
SSID: AZELCTRL_4D49A1
Pass: yaesu123
IP: 192.168.4.1

· · · · · · · · · · · · · · · ·
1.OK 2.Back
```

The SSID shown is the actual SSID the device is broadcasting (e.g., AZELCTRL_4D49A1). Connect to this network using the password shown. When connected, the browser automatically opens to the Rotator Control web interface. Press Btn1 to return to AP Mode menu.

#### PC Interface Submenu

Selecting "PC Interface" opens the PC interface settings menu:

```
PC Interface
------------
Baud Rate: 115200
Show Commands: Yes
Reverse Delay: 1s
Protocol (GS-232B)

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

| Option | Function |
|--------|----------|
| **Baud Rate** | Opens submenu to select USB serial baud rate: 115200, 57600, 38400, 19200, 9600 |
| **Show Commands** | Toggle Yes/No — show/hide RX/TX overlay on OLED when protocol commands are received. Footer shows current toggle action (e.g., `1.Nav 1|Yes 2.Back`) |
| **Reverse Delay** | Cycle through 0–3 seconds — delay before reversing motor direction to allow momentum to settle |
| **Protocol** | Opens submenu to select GS-232A, GS-232B, or Easycom |

**Controls**:
- Btn1 short-press: Cycle through options
- Btn1 long-press: Select current option (or toggle when on Show Commands line)
- Btn2: Go back to SETUP menu

##### Baud Rate Selection

Selecting "Baud Rate" opens:

```
Select Baud Rate
----------------
115200
57600
38400
19200
9600

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

Selecting a baud rate applies it immediately and returns to the PC Interface menu.

##### Protocol Selection

Selecting "Protocol" opens:

```
Protocol
--------
GS-232A (Yaesu)
GS-232B (Hamlib)
Easycom

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

Selecting a protocol applies it immediately and returns to the PC Interface menu.

#### Wipe All Settings

Selecting "Wipe all settings" shows a confirmation screen:

```
Wipe all settings
-----------------
This will erase ALL
saved settings.

Continue?

· · · · · · · · · · · · · · · ·
1.Wipe  2.Back
```

| Button | Action |
|--------|--------|
| **Btn1** | Erases all EEPROM settings, resets in-memory state, shows "Settings Wiped!" confirmation for 2 seconds, then reboots the device |
| **Btn2** | Cancel and return to SETUP menu |

**What is erased**: WiFi credentials, WiFi mode, calibration values, rotator model, Hamlib Server setting, PC Interface settings (baud rate, protocol, etc.). After reboot, the device enters the SETUP menu with all settings at factory defaults.

### 6.5 ELF Mode Toggle

ELF (over-the-top tracking) mode is toggled from the main Az/El control screen, not from a settings menu.

1. Press Btn1 repeatedly to cycle through options until "ELF" is selected.
2. Press Btn2 to toggle ELF mode On/Off.
3. The "Elevate To?" prompt appears, allowing you to immediately set the effective elevation or skip.

When ELF mode is enabled, "ELF" is displayed on the right side of the OLED screen next to the selected direction indicator.

### 6.6 Calibration Screens

Accessed from SETUP by selecting "Rot Calibrate". Opens the calibration menu with options for Azimuth, Elevation, or Skip.

#### Calibration Menu

```
Voltage Calibration
-------------------
Azimuth
Elevation
Skip (Az=El=3.2V)

· · · · · · · · · · · · · · · ·
1.Nav  1|Sel  2.Back
```

#### Azimuth/Elevation Calibration Screen

When calibration is running:

```
Cal-AZ: Sweep CCW
Moving CCW...
 AZ:  -180.0 ( 123mV)
 EL:    45.2 (2400mV)
· · · · · · · · · · · · · · · ·
1.Exit 2.Back
```

**Title bar**: Shows axis being calibrated (AZ or EL) and current state:
- `Sweep CCW` / `Sweep DOWN` — Moving to find minimum voltage
- `Sweep CW` / `Sweep UP` — Moving to find maximum voltage
- `Return CCW` / `Return DOWN` — Returning while pausing at cardinal headings
- `Adjust Max` — Waiting for operator to position rotator exactly at 180°
- `CALIBRATED` — Calibration complete

**Status area**: Context-sensitive messages:
- During sweep: `Moving CCW...` or `Moving DOWN...`
- During return: Shows next heading to pause at (e.g., `Next: EAST`) or countdown when paused (e.g., `Stopped at: SOUTH [3]`)
- During adjust: `Adjust Az to exactly 180 deg`

**Readings area**: Always shows both Az and El positions with voltages in millivolts. During calibration of one axis, the other axis shows its normal reading.

**Footer**:
- `1.Exit 2.Back` — During calibration: Btn1 exits calibration, Btn2 goes back
- `1.Done 2.Back` — When complete: Btn1 saves and exits, Btn2 goes back
- `1.Retry 2.Back` — On error: Btn1 restarts calibration, Btn2 goes back
- `1.Continue 2.Cancel` — During adjust max: Btn1 continues, Btn2 cancels

#### Skip Calibration

Selecting "Skip (Az=El=3.2V)" sets both axes to use fixed voltage ranges (min=0V, max=3.2V) without performing the automatic calibration sweep. Useful when the Yaesu controller's trim pots have already been adjusted to output exactly 3.2V at full scale.

### 6.7 ELF Elevation Prompt

Appears when ELF mode is toggled (enabled or disabled).

```
Elevate To?
-----------
Horizon (0) [5]
45
90
Skip

· · · · · · · · · · · · · · · ·
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
2. **Critical orientation**: The OLED must be mounted with the pin headers on the top edge. If you mount it upside down (pins on bottom), the text will display upside down on the screen.
3. Secure internally using insulation tape and/or black duct tape.
4. Ensure the display is firmly held and the screen is flush with the enclosure face.

### 7.5 Using the Ribbon Jumper Cable

The 40-way female-to-female ribbon jumper cable is purchased as a convenient source of coloured jumper wires — **it is not used as a single ribbon cable**. You will strip it apart and use the individual wires for various connections throughout the build.

**How to use the ribbon jumper cable**:

1. **Separate individual wires**: Pull apart the ribbon cable to get individual female-to-female jumper wires. Each wire has a female header connector on both ends and is a different colour, making identification easy.

2. **Keep wires bundled where possible**: When connecting multiple wires to the same destination (e.g., several wires going to the motor shield's GPIO headers), keep them bundled together as a small group of 2–4 wires. This makes the wiring neater and easier to trace.

3. **Solder where practical**: For connections to the ADS1115 ADC board, **cut off the female header connector on one end and solder the bare wire directly to the board's pads**. The ADS1115 module typically does not come with pin headers pre-soldered — this is intentional and convenient. Direct soldering is much more reliable than using jumper connectors, which can wobble loose over time due to vibration from the rotator motors.

4. **I2C splitter cables**: The I2C bus requires a "Y-splitter" to connect both the OLED display and the ADS1115 to the same SDA and SCL lines. Make these splitter cables from the ribbon jumper wires:
   - Take one jumper wire, cut it in the middle
   - Strip the insulation from both cut ends
   - Twist each cut end around a third jumper wire (the "branch")
   - Solder each joint and insulate with heat shrink or insulation tape
   - Repeat for both SDA and SCL lines

5. **Motor shield connections**: For wires going to the motor shield's GPIO pin headers (D1–D8), use the female header connector on one end of the jumper wire plugged into the shield. On the other end:
   - If connecting to a button: cut off the female connector and solder the bare wire to the button terminal
   - If connecting to the ADS1115: cut off the female connector and solder the bare wire to the board's pads

**Summary**: Think of the ribbon jumper cable as a convenient source of pre-coloured, pre-tinned jumper wires with connectors already crimped on one end. You'll use the connector end where it plugs into a header (like the motor shield's GPIO pins), and cut and solder the other end wherever a more permanent, reliable connection is needed.

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

**How I2C works (brief explanation)**: I2C is a communication method that uses just two wires — SDA (data) and SCL (clock). Multiple devices can share these same two wires simultaneously. Each device has a unique address, so when the ESP8266 wants to talk to one specific device, it sends that device's address first. The other devices on the bus simply ignore the message. This is why it's perfectly fine to connect both the OLED and the ADS1115 "in parallel" on the same two wires — they won't interfere with each other because they have different addresses (OLED = 0x3C, ADS1115 = 0x48).

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

**Heat-shrink recommendation**: When making the I2C splitter cables, slide a small piece of heat-shrink tubing onto each wire before soldering. After soldering the bare wire ends to the OLED's header pins, slide the heat-shrink over the joint and shrink it in place with a heat gun or lighter. This prevents accidental shorts between adjacent solder joints. When cutting and stripping the jumper wires for the splitters, leave enough extra length so you have room to fit the heat-shrink tubing over the solder joints.

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

**Practical tips**:

- **Do this wiring before installing the motor shield in the enclosure**. With the shield sitting on your workbench, it's much easier to position the wire ends into the screw-down terminals. If your enclosure is tight-fitting, trying to manoeuvre wires into terminal positions from inside the box can be very difficult.
- **Keep the DIN plug disconnected from the Yaesu controller** while making these connections. Once all wires are connected and terminals tightened, use a multimeter to verify each wire goes to the correct DIN pin before plugging anything in.
- Tighten all terminal screws securely.

### 7.9 Power Connections

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

### 7.10 Potentiometer Calibration (Before First Power-Up)

**IMPORTANT**: The Yaesu controller outputs ~4.5V by default. The ADS1115 can only handle up to 3.3V safely. You MUST adjust the trim pots on the Yaesu controller before connecting position feedback voltages to the ADS1115.

**Prerequisite**: The Yaesu controller must already be wired to the external azimuth/elevation rotator. The trim pot calibration is done with the actual rotator cable installed, because cable length affects voltage drop.

**CRITICAL WARNING**: If you change the length of the cable between the Yaesu controller and the rotator, you MUST redo this trim pot calibration. Shortening the cable reduces voltage drop, which means the voltage at the Yaesu controller's feedback pins will be higher than what you calibrated. If this voltage exceeds 3.3V, it will destroy the ADS1115 module and it will have to be replaced. **Always recalibrate after any cable length change.**

Steps:

1. Rotate the azimuth rotator clockwise to the limit (maximum meter deflection).
2. Locate the two trim pots on the Yaesu controller (adjacent to the meter calibration pots).
3. Measure the voltage between Pin 6 and Ground at the DIN connector.
4. Adjust the **Azimuth** trim pot until you read exactly 3.2V.
5. Rotate the azimuth rotator counter-clockwise to the limit. Verify the voltage approaches 0V.
6. Rotate the elevation rotator UP to the limit (maximum meter deflection).
7. Measure the voltage between Pin 1 and Ground at the DIN connector.
8. Adjust the **Elevation** trim pot until you read exactly 3.2V.
9. Rotate the elevation rotator DOWN to the limit. Verify the voltage approaches 0V.

Only after both pots are calibrated should you connect the feedback voltages to the ADS1115 and power up AZELCtrl.

### 7.11 Firmware Installation

1. Install the Arduino IDE or arduino-cli on your computer.
2. Install the ESP8266 board package.
3. Install required libraries: SSD1306 OLED driver, ADS1115 driver, EEPROM library.
4. Open the AZELCtrl firmware sketch.
5. Select board: "NodeMCU 1.0 (ESP-12E Module)" or equivalent.
6. Select port: Your ESP8266's serial port.
7. Upload the firmware.
8. Power up the unit and verify the OLED displays the startup sequence.

**Note**: WiFi credentials are configured via the captive portal setup process (see section 7.12), not in the source code.

### 7.12 Configuring WiFi via Captive Portal

When AZELCtrl has no WiFi credentials saved (fresh flash or after wiping all settings), you can configure WiFi using the captive portal:

1. From the SETUP menu, select "Network" → "WiFi Mode(None)".
2. AZELCtrl starts an open WiFi access point named **AZELCTRL_XXXX** (where XXXX is the last 4 digits of the device MAC address).
3. The OLED shows: "Connect phone to: AZELCTRL_XXXX" and "IP: 192.168.4.1".
4. On your phone or laptop, open WiFi settings and connect to the **AZELCTRL_XXXX** network (no password required).
5. Open a web browser and navigate to any address (e.g., 192.168.4.1). You'll see the AZELCTRL Setup page.
6. Enter your home WiFi SSID (network name) and password.
7. Tap "Save & Connect". AZELCtrl saves the credentials to EEPROM and stops the captive portal.
8. Disconnect your phone/laptop from the AZELCTRL_XXXX network.
9. On the AZELCtrl OLED, press Btn1 to return to the Network menu, then select "Connect WiFi" and press Btn2 to start connecting to your home network.
10. Wait for the connection to complete. If successful, the IP address will be displayed.

This same process can be used at any time to change WiFi credentials — simply go back to SETUP → Network → Set WiFi Creds.

---

## 8. Operating the Device

### 8.1 Power-Up Sequence

1. Connect power via USB (or VIN terminal).
2. OLED displays the splash screen for 3 seconds:
   - Product name, firmware version, date, author
3. If mandatory settings are incomplete (rotator variant, calibration, WiFi mode):
   - A "Missing Settings!" notice appears for 3 seconds
   - The device automatically starts WiFi in **Access Point (AP) mode** — look for the **AZELCTRL_XXXX** network (password: **yaesu123**)
   - The web interface is available at **http://192.168.4.1/** for immediate configuration
   - The OLED then shows the SETUP menu for completing the mandatory settings
4. Otherwise, the Boot menu appears:
   - "Rotator Ctrl" with a 3-second countdown (auto-selects if no button pressed)
   - "Setup" option
5. If "Rotator Ctrl" is selected and WiFi is configured:
   - WiFi connecting screen appears with SSID and elapsed time
   - On success: Connected screen with IP address, signal strength, 5-second countdown (press any button to skip)
   - On failure: "WiFi Failed!" screen for 2 seconds
6. OLED switches to the Az/El control screen.

### 8.2 Manual Control via Buttons

From the main Az/El control screen:

1. Press **Btn1** to cycle through options: SETUP → LEFT → RIGHT → UP → DOWN → ELF
2. Press and **hold Btn2** to move in the selected direction (LEFT/RIGHT/UP/DOWN). Release to stop.
3. Selecting SETUP and pressing Btn2 opens the SETUP menu.
4. Selecting ELF and pressing Btn2 toggles ELF (over-the-top) mode and shows the "Elevate To?" prompt.
5. When SETUP is selected, holding both Btn1+Btn2 together reboots the device.

### 8.3 Remote Control via Web Interface

The web interface provides a comprehensive browser-based control panel for operating the rotator from a phone, tablet, or computer. All orbital computation runs in your browser — no external servers required.

**Accessing the web interface:**

- **AP Mode**: On first boot (or after factory reset), the ESP automatically starts in AP mode. Connect to the **AZELCTRL_XXXX** WiFi network (password: **yaesu123**), then open a browser to **http://192.168.4.1/**.
- **Station Mode**: Connect to the same WiFi network as AZELCtrl, then open a browser to the IP address shown on the OLED. The IP is often 192.168.x.x, or 10.0.x.x on the default home network.

The web interface is fully functional even while navigating the device's OLED menus — both work independently.

**Web interface tabs:**

| Tab | Purpose |
|-----|---------|
| **Map** | Interactive world map showing satellite position, ground track, footprint, terminator, Sun/Moon |
| **Tracking** | Real-time satellite tracking with polar plot, AOS/LOS timer, frequency display |
| **Satellites** | Browse and manage satellite passes and frequencies |
| **Manage** | Add, remove, and refresh TLE data for satellites |
| **Passes** | Predict upcoming passes for the selected satellite |
| **Control** | Manual rotator control and calibration |
| **Settings** | Station location, tracking preferences, device configuration, ESP management |

#### Header Bar

The header bar is visible on all screens.

| Element | Description |
|---------|-------------|
| **AZELCtrl Satellite Tracker** | Application title |
| **Satellite dropdown (▸)** | Select any loaded satellite, the Sun, or the Moon. Use ↑/↓ arrow keys to cycle. |
| **TLE age (e.g. "2h ago")** | How old the selected satellite's TLE data is. Green (<24h), orange (24–72h), red (>72h). |
| **Connected / Disconnected** | Green dot = rotator reachable. Red = no connection. |

#### Map Tab

Displays an offline, equirectangular-projection world map rendered on an HTML Canvas — no external mapping API required.

**Map Overlays:**

| Toggle | Default | Description |
|--------|---------|-------------|
| **Day/Night** | Off | Shows the day/night terminator (sunrise/sunset lines and shaded night side) |
| **Footprint** | On | Green translucent circle showing the satellite's area of visibility |
| **Grid** | On | Latitude/longitude grid lines every 30° lat, 60° lon |
| **🌙 Moon** | Off | Shows the Moon's current sub-point |
| **☀️ Sun** | Off | Shows the Sun's current sub-point |

**Map Controls:**

Drag to pan, scroll/pinch to zoom. The **Duration** selector sets the ground-track length (30 min to 6 hours).

| Control | Description |
|---------|-------------|
| **↺ Reset View** | Re-centres the map on your station (QTH) and resets zoom to 100% |
| **⊕ Center Sat** | Re-centres the map on the satellite's current ground position |
| **◎ Follow Sat / Stop Following Sat** | Toggle that keeps the map centred on the satellite on every position update (30 s) — press again to stop and release the view |
| **ZOOM: 1 2 3 4** | Centred preset zoom buttons: 1 = 100% (full globe), 2 = 75%, 3 = 50%, 4 = 25%. Zooms anchor on your station, or on the satellite's current position while Follow Sat is active |
| **Map footer** | Colour-coded live values: **POS** (blue), **ALT** (amber), **VEL** (purple), **DIST** (green) |

Vertical panning is clamped so the view never scrolls past ±90° latitude (the poles); at 100% the map always shows the full pole-to-pole globe. The map refreshes the satellite position every 30 seconds.

#### Tracking Tab

The primary tab for operating the rotator.

**Satellite Info** shows current Azimuth and Elevation of the target, with AOS/LOS timer and range.

**Rotator Info** shows current rotator Azimuth and Elevation positions with movement arrows.

**ELF Mode Toggle**: Enable Elevation Flip Mode for passes that cross the 180° wrap boundary.

**Polar Plot**: Circular plot showing satellite trajectory (path traveled in solid blue, future path in dashed green/orange), current satellite position (blue diamond), and rotator position (green circle).

**Frequency Display** (satellite mode): Shows downlink/uplink frequencies with Doppler correction when SatNOGS data is available. The frequency table includes the following columns:

| Column | Description |
|--------|-------------|
| **DL Freq MHz** | Downlink frequency range (lower–upper) |
| **RX Doppler** | Doppler-corrected receive frequency (green = positive, red = negative) |
| **UL Freq MHz** | Uplink frequency range (lower–upper) |
| **TX Comp** | Doppler compensation for uplink (opposite sign of RX Doppler) |
| **Mode** | Operating mode (e.g., FM, USB, LSB, CW, etc.) |
| **CTCSS** | Continuous Tone-Coded Squelch System tone (editable, in Hz) |
| **Wake Tone** | Dual-tone CTCSS wake tone for FM repeaters (editable, in Hz) |

**Conditional tone column visibility**: The CTCSS and Wake Tone columns only appear when at least one visible transmitter has **FM mode** with an **uplink frequency**. For individual rows:
- If the mode is FM **and** an uplink frequency exists → editable input fields are shown
- If the mode is not FM (USB, LSB, CW, etc.) **or** no uplink frequency is present → `—` is displayed (no tone fields)

**Editing tone values**: Click into any CTCSS or Wake Tone input field to edit. Changes are saved automatically when the field loses focus (blur) or when you press Enter. Press Escape to revert to the last saved value. User-edited values are stored in your browser's local storage and take precedence over values fetched from the SatNOGS database during frequency refreshes. This allows you to correct inaccurate CTCSS data or add tones that SatNOGS does not provide.

**Dual-tone CTCSS**: Some FM repeater satellites require two tones — a **Wake Tone** to activate the transponder, and a **CTCSS** tone for normal operation. Both fields are editable independently and are only displayed for FM transmitters with an uplink frequency.

#### Satellites Tab

Displays all loaded satellites with upcoming pass information. Each row shows satellite name, AOS time, LOS time, AOS azimuth, max elevation, duration, and countdown timer. Filters allow showing only active satellites, frequencies, and hiding individual satellites. When the **Show Frequencies** filter is enabled, each satellite's pass table expands to show downlink/uplink frequencies, mode, and CTCSS tone (displayed as `—` for non-FM modes or when no uplink frequency is present).

#### Manage Tab

This tab is where you manage your satellite catalogue — adding satellites, downloading TLE orbital data, and fetching frequency information.

**Manage Added Satellites**: Shows a compact list of all satellites you have added, with TLE age indicators (time since last TLE update) and remove buttons.

**Adding Satellites**:

Three methods are available via the sub-tabs:

**CelesTrak Add** — Browse and add satellites from the CelesTrak catalogue:
1. Click **Load List from CelesTrak** to fetch the current amateur radio satellite catalogue.
2. Browse the list, filtered by frequency band if desired.
3. Click **Add** next to any satellite to import it with fresh TLE data.
4. Use **Add ALL** to import every satellite in the current view at once.
5. Click **Refresh All TLEs** at any time to update orbital data for all loaded satellites.

**Paste Add** — Add a satellite manually:
1. Enter the satellite name and NORAD ID.
2. Paste the two-line TLE (from CelesTrak, Space-Track, or another source) or just the NORAD ID.
3. Click **Add** to parse and store the satellite. TLE data will be fetched automatically if only a NORAD ID was provided.

**SatNOGS Data** — Fetch frequencies and status from the SatNOGS database:

The SatNOGS Data panel provides two data sources. Read the CORS note below first — it explains which options will work with your setup.

**Live API (direct):**
1. Select a satellite from the dropdown (or leave **All satellites** selected).
2. Click **Refresh Status** to fetch active/inactive status from db.satnogs.org.
3. Click **Refresh Frequencies** to download mode, frequency, and Doppler correction data.
4. These fetch from the live SatNOGS API, optionally proxied through the ESP.

**Alt Sync (pre-cached CSV):**
Located below the live API section, these buttons fetch from pre-generated CSV files hosted online. This bypasses the CORS/proxy problems entirely and is much faster. Data may be less current than the live API.

1. **Alt Sync Status** — Downloads a pre-generated CSV file of satellite statuses (active/inactive/decayed) and updates all matching satellites in your catalogue.
2. **Alt Sync Frequencies** — Downloads a pre-generated CSV file of satellite frequencies and modes, updating all matching satellites.
3. Both methods display a status line showing how many satellites were updated and when the data was generated.

**CORS and Proxy Note:**

SatNOGS's API does not currently serve CORS headers, which means browsers will block JavaScript requests made directly to `db.satnogs.org`. The Settings tab provides three proxy modes to work around this:

| Mode | Description | Best For |
|------|-------------|----------|
| **Use ESP proxy** | Requests go through the ESP8266, which fetches from SatNOGS and returns the response. The ESP sets CORS headers so the browser accepts it. | Occasional small queries |
| **Use custom proxy URL** | You provide a separate CORS-enabled proxy server (e.g. a Python script on your PC, or a cloud function). Enter its URL in the field. | Reliable, large-scale fetching |
| **Direct (no proxy)** | Browser fetches directly from SatNOGS. **Will not work** until SatNOGS adds CORS support. | — |

**Limitations of the ESP proxy:**

- **Slow**: The ESP8266 must establish an HTTPS connection for each request. This takes approximately 30 seconds per request. Refreshing status + frequencies for 20 satellites can take over 20 minutes.
- **Limited RAM**: The ESP8266 has limited heap memory. Responses larger than approximately 4 KB will be rejected with a "Response too large" error. Some satellites in the SatNOGS database have dozens of frequency entries that together exceed this limit.
- **Recommendation**: For reliable operation with a full satellite catalogue, use a **Custom proxy URL** (a lightweight CORS proxy running on your PC or LAN) or use **Alt Sync** for the pre-cached data.

**SatNOGS Proxy settings** are located in the **Settings** tab under the **SatNOGS Proxy** card.

Data from both sources is cached in your browser and used to display frequency tables with Doppler-shift-compensated uplink/downlink frequencies on the Tracking tab.

**TLE and Frequency Data Storage**: All satellite data (TLEs, frequencies, status) is stored in your browser's local storage. This means:
- Data persists across browser sessions on the same device.
- Clearing your browser data will remove your satellite list.
- Each device/browser combination has its own satellite list — there is no server-side storage.

#### Passes Tab

Shows detailed pass predictions for the selected satellite, Sun, or Moon. Select lookahead window (24/48/72 hours). Lists each pass with AOS time, LOS time, AOS azimuth, max elevation, and duration.

#### Control Tab

Shows current rotator position. Provides a cross-shaped control pad with UP, DOWN, LEFT, RIGHT, and STOP buttons (press and hold to move, release to stop). Manual position entry allows setting precise azimuth/elevation values. Start Calibration button initiates the potentiometer calibration sweep (same as OLED menu).

#### Settings Tab

**Ground Station Location**: Enter Maidenhead grid square or lat/lon/altitude.

**Tracking Preferences**: Update interval, minimum elevation, azimuth/elevation speed, lead angle, dead zone.

**Controller**:
| Field | Description |
|-------|-------------|
| **Rotator Model** | Select N360, S360, N450, or S450. Click **Set** to apply. Changing model clears calibration. |
| **Calibration** | Displays status. **Load from ESP** refreshes values. **Skip Calibration** sets default 0–3.2V range. Manual voltage entry for Az/El Min/Max. |

**WiFi**:
| Field | Description |
|-------|-------------|
| **WiFi Mode** | Select **Access Point**, **Station**, or **Disabled**. Click **Apply** to switch (disconnect warning shown). |
| **Station SSID / Password** | Enter home WiFi credentials and click **Save**. |
| **AP Password** | Change the 8-character AP password. |
| **Current AP SSID** | Read-only display of the ESP's AP network name. |

**PC Interface**:
| Field | Description |
|-------|-------------|
| **Protocol Variant** | Select GS-232A, GS-232B, or Easycom. Click **Set** to apply. |
| **Baud Rate** | Select serial baud rate (9600–115200). |
| **Show Commands** | Toggle display of received commands on the OLED. |
| **Reverse Delay** | Seconds to wait before reversing direction (0–3s). |

**Network**:
| Field | Description |
|-------|-------------|
| **Hamlib TCP Server** | Toggle the rotctld-compatible server on port 4533. |

**ESP Management**: Upload web interface, upload any file, list/delete files on the ESP filesystem.

**Debug**: JavaScript logging toggle, export/load configuration JSON.

#### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| **↑/↓ arrows** (dropdown focused) | Cycle through satellites |
| **Ctrl+Shift+R** | Hard refresh browser cache |
| **F12** | Open browser developer console |

### 8.4 Connecting Tracking Software

AZELCtrl supports two distinct methods of satellite tracking:

**Method 1 — Browser-based Tracking (Built-In)**: AZELCtrl's web interface includes a full satellite tracking engine that runs entirely in your browser using the satellite.js library. All orbital computations (position prediction, AOS/LOS timing, Doppler shift, polar plots, world map) happen in the browser — no external software required. Simply:

1. Connect to the web interface (via AP or Station mode — see section 8.3).
2. Go to the **Manage** tab to add satellites and download TLE/frequency data (see [Manage Tab](#manage-tab) in section 8.3).
3. Select a target satellite from the header dropdown.
4. Go to the **Tracking** tab and click **Start** to begin automated tracking.

This is the simplest method and works on any device with a browser — phone, tablet, laptop, or desktop.

**Method 2 — External Tracking Software**: AZELCtrl can also be controlled from dedicated satellite tracking software (Gpredict, Rotor, etc.) via USB serial or TCP/IP. Choose whichever interface suits your setup. This method gives you access to advanced features like predictive pass planning and multi-satellite scheduling that dedicated desktop software provides.

The sections below describe the connection details for external software.

#### USB Serial Connection

1. Connect AZELCtrl to your computer via USB cable.
2. Your operating system will recognize it as a virtual COM port:
   - **Windows**: Check Device Manager → Ports (COM & LPT) — typically COM3 or higher
   - **macOS**: /dev/cu.usbserial-* (check System Information → USB)
   - **Linux**: /dev/ttyUSB0 or /dev/ttyUSB1
3. Ensure AZELCtrl's **Hamlib Server** setting is **No** (so it speaks GS-232/Easycom over serial).
4. In your tracking software, configure a new rotator connection:
   - Type: GS-232B (or your configured protocol)
   - Connection: Serial / COM port
   - Port: Your virtual COM port (e.g., COM3)
   - Baud rate: 115200
5. Test the connection.

#### TCP/IP Network Connection

1. Ensure AZELCtrl is connected to your WiFi network (check OLED for IP address).
2. In your tracking software, configure a new rotator connection:
   - Type: Depends on Hamlib Server setting (see below)
   - Connection: TCP/IP
   - Host: AZELCtrl's IP address (shown on OLED)
   - Port: 4533
3. Match the Hamlib Server setting to your software:
   - **Hamlib Server = Yes**: Software expects rotctl text protocol (Gpredict)
   - **Hamlib Server = No**: Software expects GS-232/Easycom (Rotor, others)

#### Gpredict

Gpredict supports both USB serial and TCP/IP connections.

**Via TCP/IP (recommended)**:
1. Open Gpredict preferences → Satellite → Control → Rotator control.
2. Set type to "Hamlib rotctld".
3. Set host to AZELCtrl's IP address.
4. Set port to 4533.
5. **Critical**: Ensure AZELCtrl's **Hamlib Server** setting is **Yes** (under Network in the SETUP menu). Without this, AZELCtrl will speak GS-232/Easycom over TCP instead of the rotctl protocol that Gpredict expects, and the connection will fail.
6. Enable tracking for the desired satellite.

**Via USB Serial (using rotctld as bridge)**:
Gpredict does not connect directly to serial ports. It requires the Hamlib rotctld daemon as an intermediary.

1. Connect AZELCtrl to your computer via USB.
2. Ensure AZELCtrl's **Hamlib Server** setting is **No** (so it speaks GS-232/Easycom over serial).
3. Start rotctld, pointing it at AZELCtrl's virtual COM port with the matching protocol (e.g., GS-232B at 115200 baud).
4. In Gpredict preferences → Satellite → Control → Rotator control:
   - Set type to "Hamlib rotctld"
   - Set host to localhost (or wherever rotctld is running)
   - Set port to the port rotctld is listening on (typically 4532)
5. Enable tracking.

The connection path is: Gpredict → rotctld → AZELCtrl (USB serial).

**Note on ELF (over-the-top) mode**: Gpredict implements the elevation-flip technique natively. When a satellite pass crosses the rotator's wrap-around boundary, Gpredict automatically transforms the coordinates and sends the appropriate physical targets. **Do not enable ELF mode on AZELCtrl when using Gpredict** — doing so would apply the transformation twice, resulting in incorrect antenna pointing.

#### Rotor

Rotor supports both USB serial and TCP/IP connections.

**macOS users**: Rotor works well with AZELCtrl via USB serial. However, Rotor's TCP/IP interface currently does not support elevation control, making it unsuitable for satellite tracking with an azimuth-elevation rotator. Use USB serial or Gpredict for full Az/El control.

**Via TCP/IP**:
1. Configure a new rotator connection.
2. Set type to GS-232B (or your configured protocol).
3. Set connection to TCP/IP.
4. Set host to AZELCtrl's IP address.
5. Set port to 4533.
6. Ensure AZELCtrl's Hamlib Server setting is **No**.
7. Test the connection.

**Via USB Serial**:
1. Connect AZELCtrl to your computer via USB.
2. Configure a new rotator connection.
3. Set type to GS-232B (or your configured protocol).
4. Set connection to Serial.
5. Select the virtual COM port for AZELCtrl.
6. Set baud rate to 115200.
7. Ensure AZELCtrl's Hamlib Server setting is **No**.
8. Test the connection.

#### Other Tracking Software

Many satellite tracking programs support serial connections but not TCP/IP. AZELCtrl's USB serial interface works with any software that supports GS-232A, GS-232B, or Easycom protocols. Simply connect via USB and configure the virtual COM port as you would for any serial rotator controller.

### 8.5 Configuring via OLED Menus

- **Protocol**: From main screen, cycle to "SETUP" → press Btn2 → select "PC Interface" → select "Protocol" → choose GS-232A/GS-232B/Easycom.
- **Hamlib Server**: SETUP → "Network" → select "Hamlib Server" → toggle Yes/No.
- **Rotator Variant**: SETUP → select "Rot Variant" → choose N360/S360/N450/S450.
- **ELF Mode**: From main screen, cycle to "ELF" → press Btn2 → toggle On/Off → respond to "Elevate To?" prompt.
- **WiFi Credentials (Captive Portal)**: SETUP → "Network" → "WiFi Mode" → "Station" → "Set WiFi Creds" → connect phone/laptop to AZELCTRL_XXXX → open browser to configure → press Btn1 to return.
- **Review Saved Credentials**: SETUP → "Network" → "WiFi Mode" → "Station" → "Review Creds" to view saved SSID and masked password.
- **Connect/Disconnect WiFi**: SETUP → "Network" → "WiFi Mode" → "Station" → scroll to "Connect WiFi" → press Btn2. Label changes to "Disconnect WiFi" when connected.
- **Show IP Address**: SETUP → "Network" → "WiFi Mode" → "Station" → "Show IP".
- **AP Mode (Field)**: SETUP → "Network" → "WiFi Mode" → "Station" → "AP Mode (Field)" — starts AP mode immediately without changing the saved WiFi mode. The "AP Mode" menu also provides "AP Status" and "Regen AP Password" options.
- **Regen AP Password**: SETUP → "Network" → "WiFi Mode" → "Access Point" → "Regen AP Password" → press Btn1 repeatedly for entropy → note the new password.
- **Calibrate Pots**: SETUP → "Rot Calibrate" → select Azimuth or Elevation → follow the sweep prompts. See section 6.6.
- **Skip Calibration**: SETUP → "Rot Calibrate" → "Skip (Az=El=3.2V)" — sets fixed 0–3.2V range without sweeping.
- **Wipe All Settings**: SETUP → "Wipe all settings" → "Continue" to erase all EEPROM data and reboot.

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

### 9.7 Web Interface Issues

| Possible Cause | Solution |
|---------------|----------|
| Blank page after firmware flash | No HTML uploaded yet — upload `index.html.gz` via Settings tab |
| Blank page or `ERR_CONTENT_DECODING_FAILED` | `index.html.gz` was double-compressed — upload the `.gz` file, not the raw `.html` |
| Chrome shows `ERR_CONNECTION_RESET` repeatedly | ESP8266 TCP backlog fills up — use Firefox, or go to `chrome://flags/#unsafely-treat-insecure-origin-as-secure` and add the ESP's IP |
| Browser shows old version | Hard refresh (Cmd+Shift+R / Ctrl+F5) |
| Web page loads but map has no coastlines | `world_coastline.js` not uploaded — upload via Settings tab |
| Favicon not showing in browser tab | Use Chrome/Firefox, or clear browser cache ||

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
| `C` | Query position (azimuth, elevation) | `C\r` | `C125.45\r` (Az=125°, El=45°) |
| `C1` | Position with rate of change | `C1\r` | `AZ=125 EL=045 RATE=00.0\r` |
| `C2` | Position with status | `C2\r` | `AZ=125 EL=045\r` |
| `C3` | Position with full status | `C3\r` | `AZ=125 EL=045 STATUS=0\r` |
| `H` | Query azimuth heading | `H\r` | `H125\r` |
| `Hnnn` | Set azimuth to nnn° | `H180\r` | `OK\r` |
| `Mnnn` | Move azimuth to nnn° | `M180\r` | `OK\r` |
| `Wnnn eee` | Move to azimuth nnn, elevation eee | `W180 045\r` | `OK\r` |
| `V` | Query elevation | `V\r` | `V45\r` |
| `Vnnn` | Set elevation to nnn° | `V90\r` | `OK\r` |
| `L` | Azimuth left (CCW) | `L\r` | `OK\r` |
| `R` | Azimuth right (CW) | `R\r` | `OK\r` |
| `U` | Elevation up | `U\r` | `OK\r` |
| `D` | Elevation down | `D\r` | `OK\r` |
| `S` | Stop all movement | `S\r` | `OK\r` |
| `A` | All stop (alternate) | `A\r` | `OK\r` |
| `E` | Stop elevation only | `E\r` | `OK\r` |
| `Z` | Stop azimuth only | `Z\r` | `OK\r` |
| `Xnn` | Set speed (1=slow, 4=fast) | `X4\r` | `OK\r` |

#### Easycom (EasyComm 2/3) Commands

| Command | Description | Example | Response |
|---------|-------------|---------|----------|
| `AZ` | Query azimuth | `AZ\r` | `AZ125.0\r` |
| `AZ xxx.x` | Set azimuth | `AZ 180.0\r` | `OK\r` |
| `EL` | Query elevation | `EL\r` | `EL45.0\r` |
| `EL yy.y` | Set elevation | `EL 90.0\r` | `OK\r` |
| `ML` | Move azimuth left (CCW) | `ML\r` | `OK\r` |
| `MR` | Move azimuth right (CW) | `MR\r` | `OK\r` |
| `MU` | Move elevation up | `MU\r` | `OK\r` |
| `MD` | Move elevation down | `MD\r` | `OK\r` |
| `S` | All stop | `S\r` | `OK\r` |
| `SA` | Stop azimuth only | `SA\r` | `OK\r` |
| `SE` | Stop elevation only | `SE\r` | `OK\r` |
| `VL` | Velocity — move left | `VL\r` | `OK\r` |
| `VR` | Velocity — move right | `VR\r` | `OK\r` |
| `VU` | Velocity — move up | `VU\r` | `OK\r` |
| `VD` | Velocity — move down | `VD\r` | `OK\r` |
| `VE` | Query firmware version | `VE\r` | `VEYCINT/1.0\r` |
| `GS` | Get status (1=Idle, 2=Moving) | `GS\r` | `GS1\r` |
| `AO` | AOS acknowledge | `AO\r` | `OK\r` |
| `LO` | LOS acknowledge | `LO\r` | `OK\r` |
| `\?` | Backslash query (rotctl identification) | `\?\r` | `\?EASYCOM,ROTATOR\r` |

#### rotctl Commands

| Command | Description | Example | Response |
|---------|-------------|---------|----------|
| `p` | Query position | `p\r\n` | `125\n45\nRPRT 0\n` |
| `P az el` | Move to position | `P 180 45\r\n` | `RPRT 0\n` |
| `S` | Stop | `S\r\n` | `RPRT 0\n` |

### 10.4 EEPROM Layout

| Bytes | Size | Content | Magic Byte |
|-------|------|---------|------------|
| 0–66 | 67 | WiFi SSID (data at 1–32) and password (data at 34–65) | 0xAA at byte 66 |
| 67–83 | 17 | Calibration min/max voltages (4× float) | 0xCA at byte 83 |
| 84–85 | 2 | WiFi mode (STA/AP/Disabled) | 0x4D at byte 85 |
| 86–87 | 2 | Network settings (Hamlib Server toggle) | 0x4E at byte 87 |
| 88–89 | 2 | Rotator model (N360/S360/N450/S450) | 0x5A at byte 89 |
| 90 | 1 | ELF mode enabled/disabled | — |
| 91–99 | 9 | AP password (8 chars) | 0x41 ('A') at byte 99 |
| 100–104 | 5 | Protocol settings — baud index, show commands, reverse delay, protocol variant | 0xB7 at byte 102 |

---

*End of Document*

*AZELCtrl — Built for amateur satellite operators, by amateur satellite operators.*

*73 ZS6TW*
