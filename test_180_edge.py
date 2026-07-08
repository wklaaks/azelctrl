#!/usr/bin/env python3
"""
Edge case test for N360 180° mechanical limit.

Tests that the rotator takes the correct long-path around when
the target is on the other side of the 180° physical stop.

Test sequence:
  1. Read current position
  2. Move to 175° (just below 180° stop)
  3. Move to 185° (just above 180° stop) — MUST go CW through 360/0 (190° travel)
  4. Move to 175° again — MUST go CCW through 0 (190° travel)
  5. Move to 175° again — short path CCW (10° travel, no wrap)

Usage:
    python3 test_180_edge.py <serial_port>
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


def main():
    parser = argparse.ArgumentParser(description='N360 180° edge case test')
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

    def wait_for_target(target: float, timeout: float = 120.0, tolerance: float = 1.5) -> float:
        """Poll until target is reached and stable, or timeout."""
        start = time.time()
        stable_count = 0
        last_az = None
        while time.time() - start < timeout:
            time.sleep(1)
            resp = send('AZ', delay=0.3)
            cur_az = parse_az(resp)
            if cur_az is not None:
                elapsed = time.time() - start
                # Calculate shortest diff for display
                diff = abs(cur_az - target)
                if diff > 180:
                    diff = 360 - diff
                # Check stability
                if last_az is not None and abs(cur_az - last_az) < 0.5:
                    stable_count += 1
                else:
                    stable_count = 0
                last_az = cur_az
                print(f"    Az={cur_az:.1f}° (target {target:.1f}°, diff {diff:.1f}°) [{int(elapsed)}s]{' (stable)' if stable_count >= 2 else ''}")
                if diff <= tolerance and stable_count >= 2:
                    return cur_az
            else:
                stable_count = 0
        return last_az

    print("Escaping any blocking menu...")
    send('MODE:ROTATOR', delay=0.5)
    time.sleep(0.5)

    # Read current position
    print("\nReading current position...")
    resp = send('AZEL', delay=1.0)
    print(f"  Response: {resp}")
    az = parse_az(resp)
    if az is None:
        print("  ERROR: Could not parse position")
        ser.close()
        return
    print(f"  Current: Az={az:.1f}°")

    # Step 1: Move to 175° (just below the 180° stop)
    print(f"\n[Step 1] Moving AZ to 175° (just below 180° stop)...")
    resp = send('AZ175', delay=1.0)
    print(f"  Response: {resp}")
    result = wait_for_target(175.0)
    if result is not None:
        print(f"  ✓ Reached Az={result:.1f}°")
    else:
        print("  ✗ Timeout")

    # Step 2: Move to 185° — MUST go CW through 360/0 (long path ~190°)
    print(f"\n[Step 2] Moving AZ to 185° (just above 180° stop)...")
    print("  Expected: CW rotation through 360/0° (long path ~190°)")
    resp = send('AZ185', delay=1.0)
    print(f"  Response: {resp}")
    result = wait_for_target(185.0, timeout=120.0)
    if result is not None:
        print(f"  ✓ Reached Az={result:.1f}°")
    else:
        print("  ✗ Timeout — rotator may have taken wrong direction")

    # Step 3: Move to 175° — MUST go CCW through 0 (long path ~190°)
    print(f"\n[Step 3] Moving AZ to 175° (back below 180° stop)...")
    print("  Expected: CCW rotation through 0° (long path ~190°)")
    resp = send('AZ175', delay=1.0)
    print(f"  Response: {resp}")
    result = wait_for_target(175.0, timeout=120.0)
    if result is not None:
        print(f"  ✓ Reached Az={result:.1f}°")
    else:
        print("  ✗ Timeout — rotator may have taken wrong direction")

    # Step 4: Move to 165° — short path CCW (~10°)
    print(f"\n[Step 4] Moving AZ to 165° (short path CCW ~10°)...")
    print("  Expected: CCW rotation (short path ~10°)")
    resp = send('AZ165', delay=1.0)
    print(f"  Response: {resp}")
    result = wait_for_target(165.0, timeout=30.0)
    if result is not None:
        print(f"  ✓ Reached Az={result:.1f}°")
    else:
        print("  ✗ Timeout")

    # Step 5: Move to 175° — short path CW (~10°)
    print(f"\n[Step 5] Moving AZ to 175° (short path CW ~10°)...")
    print("  Expected: CW rotation (short path ~10°)")
    resp = send('AZ175', delay=1.0)
    print(f"  Response: {resp}")
    result = wait_for_target(175.0, timeout=30.0)
    if result is not None:
        print(f"  ✓ Reached Az={result:.1f}°")
    else:
        print("  ✗ Timeout")

    print("\nTest complete — press Btn1 on the device to exit PC mode.")
    ser.close()


if __name__ == '__main__':
    main()
