#!/usr/bin/env python3
"""
Quick button test — reads position, moves Azimuth +5°, then waits.
Press Btn1 on the device to exit PC mode and cycle directions.

Usage:
    python3 test_btn.py <serial_port>
"""

import sys
import time
import argparse
import serial
import re


def parse_az(response: str) -> float | None:
    if response.startswith('AZEL'):
        m = re.match(r'AZEL(\d+\.\d+)', response)
        if m:
            return float(m.group(1))
    m = re.match(r'AZ(\d+\.\d+)', response)
    if m:
        return float(m.group(1))
    return None


def parse_el(response: str) -> float | None:
    if response.startswith('AZEL'):
        parts = response.split('.')
        if len(parts) >= 3:
            try:
                return float(parts[2])
            except ValueError:
                pass
    m = re.match(r'EL(\d+\.\d+)', response)
    if m:
        return float(m.group(1))
    return None


def main():
    parser = argparse.ArgumentParser(description='Quick button test')
    parser.add_argument('port', help='Serial port')
    parser.add_argument('--baud', type=int, default=115200)
    args = parser.parse_args()

    try:
        ser = serial.Serial(port=args.port, baudrate=args.baud, timeout=2)
        time.sleep(5)
        while ser.in_waiting:
            ser.readline()
        time.sleep(1)
        print(f"Connected to {args.port} at {args.baud} baud")
    except serial.SerialException as e:
        print(f"Failed to connect: {e}")
        sys.exit(1)

    def send(cmd: str, delay: float = 0.5) -> str:
        ser.reset_input_buffer()
        time.sleep(0.1)
        ser.write((cmd + '\r\n').encode('ascii'))
        time.sleep(delay)
        response = ""
        while ser.in_waiting:
            line = ser.readline().decode('ascii', errors='replace').strip()
            skip = ('DBG:', 'ROT:', 'BOOT:', 'SETUP:', 'Action:', 'WiFi',
                    'AP Mode', 'Yaesu', 'Rotator Ctrl', 'MODE:', 'Rotator Control')
            if any(line.startswith(p) for p in skip):
                continue
            if line:
                response = line
        return response

    print("Escaping any blocking menu...")
    send('MODE:ROTATOR', delay=0.5)
    time.sleep(0.5)

    # Read current position
    print("\nReading current position...")
    resp = send('AZEL', delay=1.0)
    print(f"  Response: {resp}")
    az = parse_az(resp)
    el = parse_el(resp)
    if az is None or el is None:
        print("  ERROR: Could not parse position")
        ser.close()
        return
    print(f"  Current: Az={az:.1f}°, El={el:.1f}°")

    # Move Azimuth -5°
    target = az - 5.0
    if target < 0.0:
        target += 360.0
    print(f"\nMoving Azimuth to {target:.1f}° (-5°)...")
    resp = send(f'AZ{target:.0f}', delay=1.0)
    print(f"  Response: {resp}")

    print("\nWaiting for movement to complete — then press Btn1 to test button mode...")
    print("The OLED should show RX/TX lines. Press Btn1 to clear and return to button mode.")
    print("Then press Btn1 again to cycle through directions.")

    # Poll until stable
    for i in range(60):
        time.sleep(1)
        resp = send('AZ', delay=0.5)
        cur_az = parse_az(resp)
        if cur_az is not None:
            diff = abs(cur_az - target)
            if diff > 180:
                diff = 360 - diff
            print(f"  Az={cur_az:.1f}° (diff {diff:.1f}°)")
            if diff <= 1.0:
                print(f"\n✓ Reached target — Az={cur_az:.1f}°")
                break
        else:
            print(f"  (no response)")

    print("\nTest complete — press Btn1 on the device to test button mode.")
    ser.close()


if __name__ == '__main__':
    main()
