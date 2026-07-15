#!/usr/bin/env python3
"""Capture ESP8266 boot sequence. Waits for serial port to appear (power cycle)."""
import serial
import time
import sys

PORT = "/dev/cu.usbserial-0001"
BAUD = 115200

print(f"Waiting for {PORT} (power cycle the device now)...")
ser = None
for attempt in range(60):
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        time.sleep(1)  # Wait for device to stabilize
        break
    except serial.SerialException:
        if attempt % 10 == 0:
            print(f"  Port not ready... ({attempt}s)")
        time.sleep(1)
else:
    print("Timeout waiting for port.")
    sys.exit(1)

# Drain any initial garbage
time.sleep(0.5)
ser.reset_input_buffer()

print(f"\n=== Capturing output from {PORT} ===\n")
lines = []
idle_start = None

try:
    while True:
        line = ser.readline().decode("utf-8", errors="replace").rstrip("\r\n")
        if line:
            if idle_start and (time.time() - idle_start > 1):
                print("\n--- New activity ---\n")
            idle_start = None
            lines.append(line)
            # Highlight key lines
            highlight = False
            for kw in ["CAL:", "EEPROM[", "ROT:", "WIFI:", "Missing",
                       "Mandatory", "================================",
                       "Yaesu", "BOOT:"]:
                if kw in line:
                    highlight = True
                    break
            if highlight:
                print(f"\033[93m{line}\033[0m")
            else:
                print(line)
        else:
            if idle_start is None:
                idle_start = time.time()
except KeyboardInterrupt:
    pass
finally:
    if ser:
        ser.close()

# Print summary of key lines
print("\n" + "=" * 50)
print("SUMMARY — Boot diagnostics:")
print("=" * 50)
for line in lines:
    if any(kw in line for kw in ["CAL:", "EEPROM[", "ROT:", "WIFI:",
                                  "Missing", "Mandatory", "BOOT:"]):
        print(line)
