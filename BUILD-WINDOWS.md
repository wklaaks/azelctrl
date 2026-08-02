# AZELCtrl — Windows Flashing & Setup Guide

This guide walks Windows users through flashing a **brand new ESP8266** with the AZELCtrl firmware and uploading all required support files.

> 📺 **Video tutorial:** [How to Flash an ESP8266 / ESP32 (YouTube)](https://youtu.be/74NGHj-cOls) — a step-by-step walkthrough of the flashing process.

---

## 1. What You Need

### Hardware

| Item | Notes |
|------|-------|
| **ESP8266 board** (NodeMCU v2/v3 or Wemos D1 Mini) | Any board with 4 MB flash |
| **USB cable** | Data-capable (not charge-only). Try 2–3 cables if upload fails. |
| **Computer running Windows** | Windows 10 or 11 |

### Software — Download & Install

| Tool | Why | Where to Get It |
|------|-----|-----------------|
| **CH340 driver** | Makes the ESP8266 visible on a COM port | If your board uses a **CH340G** chip (most NodeMCU clones): [CH340 driver (official)](https://www.wch.cn/download/CH341SER_EXE.html) |
| **CP210x driver** | Alternative USB chip (some NodeMCU v3 / Wemos D1 Mini) | [CP210x driver (Silicon Labs)](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) |
| **A Chromium-based browser** (Chrome, Edge, or Firefox 151+) | Used with the web-based flashing tool | Already installed on most Windows PCs |
| **Arduino IDE** (v2.x) — *alternative method* | Flashes the firmware via traditional tool | [arduino.cc/en/software](https://www.arduino.cc/en/software) — download the **Windows Win 10/11** installer |
| **7-Zip** (optional) | Extract the firmware archive if provided as `.zip` | [7-zip.org](https://7-zip.org/) |

> **Which driver?** Check your board's USB chip: if you see a small rectangular chip labelled **CH340** — install the CH340 driver. If labelled **CP2102** — install the CP210x driver. When in doubt, install both.

### Files You Need to Download

From the AZELCtrl release page you will need **four files**:

| File | Purpose |
|------|---------|
| `azelctrl-firmware.bin` | The compiled firmware (~572 KB) — this is the main program |
| `index.html.gz` | The satellite tracker web interface (gzip-compressed) |
| `favicon.ico` | Browser tab icon (~1 KB) |
| `world_coastline.js` | World map coastline data for the orbital map |

> The `.bin` file is the compiled firmware. The remaining three files are uploaded to the ESP's filesystem (SPIFFS) after flashing — they are **not** part of the firmware itself.

---

## 2. Install Drivers & Identify Your COM Port

Before flashing, your PC needs to communicate with the ESP8266 over USB.

1. **Connect the ESP8266** to your computer via USB.
2. Open **Device Manager** (right-click Start → Device Manager).
3. Look under **Ports (COM & LPT)** for one of these:
   - `USB-SERIAL CH340` (COMx) — driver installed ✓
   - `Silicon Labs CP210x USB to UART Bridge` (COMx) — driver installed ✓
   - `Unknown device` with a yellow triangle — **driver not installed** → run the CH340 or CP210x installer above, then reconnect the board.
4. **Note the COM port number** (e.g. `COM3`, `COM5`). You'll need it later if using the Arduino IDE method.

> If the board still shows as unknown after installing the driver, try a different USB port or cable. Many "charge-only" cables don't carry data.

---

## 3. Flash the Firmware (Recommended: ESPHome Web Tool)

The easiest way to flash the firmware on Windows is using **ESPHome Web** — a browser-based tool that works over Web Serial. No software installation needed beyond the USB drivers.

> ⚠️ **Browser requirements:** ESPHome Web requires Web Serial support.
> - ✅ **Google Chrome**, **Microsoft Edge**, or any **Chromium-based** browser
> - ✅ **Firefox 151 or newer**
> - ❌ **Safari** — does not support Web Serial
> - ❌ Firefox managed by an organisation may have Web Serial disabled by policy

📺 **See it in action:** The [flashing tutorial video](https://youtu.be/74NGHj-cOls) demonstrates the same process.

### Steps

1. **Open** [https://web.esphome.io](https://web.esphome.io) in Chrome, Edge, or Firefox 151+.
2. **Connect the ESP8266** to your computer via USB (if not already connected).
3. Click the **"CONNECT"** button on the page.
4. A browser dialog will appear listing available serial ports. Select the COM port for your ESP8266 (e.g. `COM3`). Click **Connect**.
5. Once connected, you'll see the ESPHome Web interface. Click the **"INSTALL"** button (the one labelled **Install**, *not* "Prepare for first use").
6. Click **"CHOOSE FILE"** and navigate to the `azelctrl-firmware.bin` file you downloaded from the release page.
7. The tool will flash the firmware to the ESP8266. A progress bar shows the status.
8. When complete, the ESP will automatically reboot.

The firmware is now flashed ✓

> **Troubleshooting:** If the ESP is not detected or flashing fails:
> - Make sure the USB drivers are installed (Section 2).
> - Try a different USB cable (data-capable).
> - Close other programs that might be using the COM port (serial monitors, Arduino IDE).
> - Press the **RST** button on the ESP8266 and try reconnecting.

---

## 4. Alternative: Flash via Arduino IDE

If you prefer a traditional desktop application, or the web-based tool doesn't work for your setup, you can use the Arduino IDE.

### Install Arduino IDE + ESP8266 Support

1. Download and install the Arduino IDE from [arduino.cc/en/software](https://www.arduino.cc/en/software).
2. Launch Arduino IDE.
3. Go to **File → Preferences** (or **Ctrl + ,**).
4. In the **"Additional Boards Manager URLs"** field, paste:
   ```
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
5. Click **OK**.
6. Go to **Tools → Board → Boards Manager**.
7. Search for `esp8266`.
8. Find **"esp8266 by ESP8266 Community"** and click **Install** (version 3.1.2 or later).
9. Wait for the download to finish — this may take a few minutes.

### Board Settings

1. Go to **Tools → Board → ESP8266 Boards** and select **"NodeMCU 1.0 (ESP-12E Module)"**.
2. Set these board options:

   | Setting | Value |
   |---------|-------|
   | Board | `NodeMCU 1.0 (ESP-12E Module)` |
   | Flash Size | `4MB (FS:3MB OTA:~512KB)` |
   | Flash Mode | `DIO` |
   | CPU Frequency | `80 MHz` |
   | Upload Speed | `115200` |
   | Port | *(your COM port, e.g. `COM3`)* |

   > **Flash Size = 4MB (FS:3MB OTA:~512KB)** is critical — this sets the 4M3M layout (~1 MB code + ~3 MB filesystem). Without this, the filesystem won't have enough space for the web interface and coastline data.

### Flash

1. Go to **Sketch → Upload** (or click the **→** arrow button in the toolbar).
2. Watch the Arduino IDE output window. You'll see:
   ```
   Connecting..._____.....
   ```
   If it hangs at `Connecting...`:
   - Press and hold the **FLASH** button on the ESP8266 board
   - Tap the **RST** button once
   - Release **FLASH**
   - The upload should start
3. When you see:
   ```
   Hash of data verified.
   Leaving...
   Hard resetting...
   ```
   — the firmware is flashed successfully ✓

---

## 5. Alternative: ESP8266 Flash Download Tool (Technical Users)

Espressif's official **Flash Download Tool** is another option for Windows:

1. Download from: [espressif.com/en/support/download/other-tools](https://www.espressif.com/en/support/download/other-tools)
   (Look for **"Flash Download Tools"** — it's a `.zip` file, extract it anywhere.)
2. Run `flash_download_tool.exe`.
3. Select **"ESP8266"** as the chip type.
4. Configure as follows:

   | Field | Value |
   |-------|-------|
   | **azelctrl-firmware.bin** | `@ 0x0` |
   | SPI Speed | `40MHz` |
   | SPI Mode | `DIO` |
   | Flash Size | `32Mbit (4MB)` |
   | COM | *(your COM port)* |
   | Baud Rate | `115200` |

5. Click **START** — the tool will erase and flash the firmware.

Then continue with **Section 7** below to upload the supporting files via the web interface.

> The Flash Download Tool can also be used to flash SPIFFS data directly, but the web interface upload method (Section 7) is simpler and doesn't risk overwriting the firmware.

---

## 6. Connect to the ESP8266 WiFi

The firmware is designed to work **straight out of the box** after flashing. On its very first boot, the ESP detects that no settings have been configured yet and **automatically starts in Access Point (AP) mode**. This lets you connect immediately to upload the supporting files — no OLED menu navigation required.

1. On your PC, open **WiFi settings**.
2. Look for a WiFi network named **`AZELCTRL_XXXX`** (where `XXXX` is a 4-digit hex code unique to your device, e.g. `AZELCTRL_A3F1`).
3. Connect to it. The password is: **`yaesu123`**.
4. Open a web browser and go to: **http://192.168.4.1/**
5. You should see the AZELCtrl web interface. The web server continues to run even while you navigate the device's OLED menus, so you can freely switch between them.

> **SSID format:** The network name is `AZELCTRL_` followed by the last 2 bytes of the ESP's MAC address in hex — for example `AZELCTRL_A3F1`. This ensures each device has a unique SSID.
>
> **Auto-AP behaviour:** The AP mode starts automatically on first boot (when no rotator model, calibration, or WiFi mode has been saved). After you complete the full setup via the OLED menu and reboot, the ESP uses whichever WiFi mode you selected (Station or AP). You can always re-enable AP mode later from the menu.

Once connected via WiFi, go straight to **Section 7** to upload the supporting files. You can complete the device setup (rotator model, calibration, WiFi mode) through the OLED menus at any time.

> If you prefer to configure your home WiFi credentials so the ESP joins your local network, use the **Settings** tab in the web interface, or the OLED menu's WiFi Station settings. After saving, the ESP will reboot and you can reach it at its DHCP-assigned IP address (check your router's client list).

---

## 7. Upload Supporting Files (SPIFFS)

Once the firmware is running and you can reach the ESP's web interface, you need to upload three additional files to its filesystem.

**Use the web interface's upload feature:**

1. Open the AZELCtrl web interface in your browser.
2. Go to the **Settings** tab (scroll to the bottom).
3. Upload each file **one at a time**:

   | # | File | Upload Method | Destination Path |
   |---|------|--------------|------------------|
   | 1 | `index.html.gz` | Use **"Upload Web Interface to ESP"** (first upload button) | `/index.html.gz` (auto) |
   | 2 | `favicon.ico` | Use **"Upload Any File"** (second upload button) | `/favicon.ico` |
   | 3 | `world_coastline.js` | Use **"Upload Any File"** | `/world_coastline.js` |

4. After each upload, you should see "Upload complete" in the status area.
5. Click **"List Files"** to confirm all three files are present.

> **For `index.html.gz`** — use the **first** upload button labelled **"Upload Web Interface to ESP"**. This sends the file to the correct path `/index.html.gz` with the required field name `html`.
>
> **For `favicon.ico` and `world_coastline.js`** — use the **second** upload button labelled **"Upload Any File"**. Select the file and click **Upload File**.

### Verification

After uploading:

1. Go to **Settings → List Files** — you should see:
   ```
   /index.html.gz
   /favicon.ico
   /world_coastline.js
   ```
2. Refresh the main page — the satellite tracker UI should load (served from SPIFFS).
3. The browser tab should show the AZELCtrl icon (favicon).
4. Go to the **Map** tab — the world map should render with coastlines.

---

## 8. Alternative: Upload via `curl` (Command Line)

If you prefer the command line or need to script the process, Windows 10/11 have curl built-in:

```batch
:: Upload HTML (web interface)
curl -F "html=@index.html.gz" http://192.168.88.154/upload

:: Upload favicon
curl -F "file=@favicon.ico" http://192.168.88.154/upload

:: Upload world coastline data
curl -F "file=@world_coastline.js" http://192.168.88.154/upload
```

> **Note:** The HTML file **must** use the field name `html` (first command). All other files use the field name `file`. The web interface handles this automatically — only needed if using `curl` directly.
>
> **Replace `192.168.88.154`** with your ESP's actual IP address.

---

## 9. Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| ESP not detected by ESPHome Web | Missing driver / bad cable | Install CH340 or CP210x driver (Section 2); try different USB cable |
| Web Serial dialog shows no ports | USB driver not installed | Install driver and reconnect the board |
| `Connecting...` timeout in Arduino IDE | Board not in flash mode | Press and hold FLASH, tap RST, release FLASH, then retry |
| `Failed to connect to ESP8266` | Wrong baud rate or port | Check COM port number in Device Manager; try 9600 baud |
| Web page loads but map has no coastlines | `world_coastline.js` not uploaded | Upload it via Settings → Upload Any File |
| Blank page or `ERR_CONTENT_DECODING_FAILED` | `index.html.gz` was double-compressed | Make sure you upload the `.gz` file, not the raw `.html` |
| Blank page after firmware flash | No HTML uploaded yet | This is normal — upload `index.html.gz` via Settings next |
| Chrome `ERR_CONNECTION_RESET` repeatedly | ESP8266's TCP backlog fills up | Use Firefox, or go to `chrome://flags/#unsafely-treat-insecure-origin-as-secure` and add `http://192.168.88.154` |
| Favicon not showing in browser tab | Safari needs proper Content-Type | Use Chrome or Firefox; or clear browser cache |
| Upload fails half-way | Large file or weak WiFi signal | Move PC closer to the ESP; or connect directly to the ESP's AP |

### Recovery: If the ESP becomes unresponsive

1. **Hard reset:** Press the RST button on the board.
2. **Re-flash:** Re-run the ESPHome Web or Arduino IDE flashing process.
3. **Factory reset:** Hold the FLASH button while powering on, or connect via serial at 115200 baud and send a reset command if a serial monitor is available.

---

## 10. File Reference Summary

| File | Flashing/Upload Method | Field Name | Destination | Size (approx) |
|------|----------------------|------------|-------------|---------------|
| `azelctrl-firmware.bin` | ESPHome Web / Arduino IDE / Flash Tool | — | Flashed at 0x0 | ~572 KB |
| `index.html.gz` | Web: "Upload Web Interface" or `curl -F "html=@..."` | `html` | SPIFFS: `/index.html.gz` | ~46 KB |
| `favicon.ico` | Web: "Upload Any File" or `curl -F "file=@..."` | `file` | SPIFFS: `/favicon.ico` | ~1 KB |
| `world_coastline.js` | Web: "Upload Any File" or `curl -F "file=@..."` | `file` | SPIFFS: `/world_coastline.js` | ~1.5 MB |

After all four files are loaded, the ESP's filesystem (SPIFFS) will contain:
- `/index.html.gz` — satellite tracker web UI (served with gzip Content-Encoding)
- `/favicon.ico` — browser tab icon
- `/world_coastline.js` — world map coastline data for the orbital map

The firmware serves the web interface from SPIFFS first, with a built-in PROGMEM fallback as backup. The coastline data and favicon are **only** available after upload — without them the map tab will show an empty globe and the browser tab will show no icon.