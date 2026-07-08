#!/usr/bin/env python3
"""
Yaesu Rotator Controller - Heading Test

Tests specific heading movements:
  1. Read start position
  2. AZ -> 25°
  3. AZ -> 315° (wait 5s)
  4. AZ -> 185° (wait 5s)
  5. AZ -> 175°
  6. EL -> 25°

Usage:
    python3 test_heading.py <serial_port>
    python3 test_heading.py /dev/cu.usbserial-0001
"""

import sys
import time
import argparse
import serial
import re
from typing import Optional


def parse_az(response: str) -> Optional[float]:
    """Extract azimuth from AZnnn.n or AZEL response."""
    if response.startswith('AZEL'):
        m = re.match(r'AZEL(\d+\.\d+)', response)
        if m:
            return float(m.group(1))
    m = re.match(r'AZ(\d+\.\d+)', response)
    if m:
        return float(m.group(1))
    return None


def parse_el(response: str) -> Optional[float]:
    """Extract elevation from ELnnn.n or AZEL response."""
    if response.startswith('AZEL'):
        m = re.match(r'AZEL\d+\.\d+\.\d+\.\d+', response)
        if m:
            parts = response.split('.')
            if len(parts) >= 3:
                return float(parts[2])
    m = re.match(r'EL(\d+\.\d+)', response)
    if m:
        return float(m.group(1))
    return None


def run_heading_test(port: str, baudrate: int = 115200):
    """Run the heading movement test."""
    print(f"\n{'='*60}")
    print("HEADING MOVEMENT TEST")
    print(f"{'='*60}")

    # Connect
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            timeout=2,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE
        )
        time.sleep(5)
        # Flush boot messages
        while ser.in_waiting:
            ser.readline()
        time.sleep(1)
        print(f"Connected to {port} at {baudrate} baud")
    except serial.SerialException as e:
        print(f"Failed to connect to {port}: {e}")
        sys.exit(1)

    def send(cmd: str, delay: float = 1.5) -> str:
        """Send a command and return the response."""
        ser.reset_input_buffer()
        time.sleep(0.2)
        ser.write((cmd + '\r\n').encode('ascii'))
        time.sleep(delay)
        response = ""
        while ser.in_waiting:
            line = ser.readline().decode('ascii', errors='replace').strip()
            skip_prefixes = ('ROT:', 'BOOT:', 'SETUP:', 'Action:', 'WiFi',
                             'AP Mode', 'Yaesu', 'Rotator Ctrl', 'MODE:',
                             'Rotator Control')
            if any(line.startswith(p) for p in skip_prefixes):
                continue
            if line:
                response = line
        return response

    # Force device into rotator control mode
    print("Sending MODE:ROTATOR to escape any blocking menu...")
    send('MODE:ROTATOR', delay=0.5)
    time.sleep(0.5)

    DELAY = 1.5  # Pause between commands for OLED visibility

    # --- Step 1: Read start position ---
    print(f"\n[Step 1] Reading start position...")
    time.sleep(DELAY)
    resp = send('AZEL', delay=DELAY)
    print(f"  Response: {resp}")
    start_az = parse_az(resp)
    start_el = parse_el(resp)
    if start_az is None or start_el is None:
        print("  ERROR: Could not parse start position. Aborting.")
        ser.close()
        return
    print(f"  Start: Az={start_az:.1f}°, El={start_el:.1f}°")
    time.sleep(DELAY)

    # --- Step 2: AZ -> 25° ---
    print(f"\n[Step 2] Moving AZ to 25.0°")
    resp = send('AZ25', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    resp = send('AZ', delay=DELAY)
    print(f"  Verify AZ: {resp}")
    az = parse_az(resp)
    if az is not None:
        print(f"  Actual: Az={az:.1f}° (target 25.0°, diff={abs(az-25.0):.1f}°)")
    time.sleep(DELAY)

    # --- Step 3: AZ -> 315° (wait 5s) ---
    print(f"\n[Step 3] Moving AZ to 315.0°")
    resp = send('AZ315', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    resp = send('AZ', delay=DELAY)
    print(f"  Verify AZ: {resp}")
    az = parse_az(resp)
    if az is not None:
        print(f"  Actual: Az={az:.1f}° (target 315.0°, diff={abs(az-315.0):.1f}°)")
    print(f"  Waiting 5 seconds at heading...")
    for i in range(5, 0, -1):
        print(f"  {i}...")
        time.sleep(1)
    time.sleep(DELAY)

    # --- Step 4: AZ -> 185° (wait 5s) ---
    print(f"\n[Step 4] Moving AZ to 185.0°")
    resp = send('AZ185', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    resp = send('AZ', delay=DELAY)
    print(f"  Verify AZ: {resp}")
    az = parse_az(resp)
    if az is not None:
        print(f"  Actual: Az={az:.1f}° (target 185.0°, diff={abs(az-185.0):.1f}°)")
    print(f"  Waiting 5 seconds at heading...")
    for i in range(5, 0, -1):
        print(f"  {i}...")
        time.sleep(1)
    time.sleep(DELAY)

    # --- Step 5: AZ -> 175° ---
    print(f"\n[Step 5] Moving AZ to 175.0°")
    resp = send('AZ175', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    resp = send('AZ', delay=DELAY)
    print(f"  Verify AZ: {resp}")
    az = parse_az(resp)
    if az is not None:
        print(f"  Actual: Az={az:.1f}° (target 175.0°, diff={abs(az-175.0):.1f}°)")
    time.sleep(DELAY)

    # --- Step 6: EL -> 25° ---
    print(f"\n[Step 6] Moving EL to 25.0°")
    resp = send('EL25', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    resp = send('EL', delay=DELAY)
    print(f"  Verify EL: {resp}")
    el = parse_el(resp)
    if el is not None:
        print(f"  Actual: El={el:.1f}° (target 25.0°, diff={abs(el-25.0):.1f}°)")
    time.sleep(DELAY)

    # --- Final position ---
    print(f"\n[Final] Reading final position...")
    resp = send('AZEL', delay=DELAY)
    print(f"  Final AZEL: {resp}")
    final_az = parse_az(resp)
    final_el = parse_el(resp)
    if final_az is not None and final_el is not None:
        print(f"  Final: Az={final_az:.1f}°, El={final_el:.1f}°")

    print(f"\n{'='*60}")
    print("HEADING TEST COMPLETE")
    print(f"{'='*60}")

    ser.close()


def main():
    parser = argparse.ArgumentParser(description='Yaesu Rotator Heading Test')
    parser.add_argument('port', help='Serial port (e.g., /dev/cu.usbserial-0001)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    args = parser.parse_args()

    run_heading_test(args.port, baudrate=args.baud)


if __name__ == '__main__':
    main()
