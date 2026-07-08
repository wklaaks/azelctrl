#!/usr/bin/env python3
"""
Direction reversal safeguard test.

1. Read current Azimuth heading
2. Move to Original + 45° (takes ~10s)
3. After 3 seconds, send new command: Original - 25°
4. Observe: firmware should detect reversal, stop motor, wait delay, then start new direction

Usage:
    python3 test_reversal.py <serial_port>
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
    parser = argparse.ArgumentParser(description='Direction reversal safeguard test')
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
    if az is None:
        print("  ERROR: Could not parse position")
        ser.close()
        return
    original_az = az
    print(f"  Original: Az={original_az:.1f}°")

    # Step 1: Move to Original + 45°
    target1 = original_az + 45.0
    if target1 > 360.0:
        target1 -= 360.0
    print(f"\n[Step 1] Moving Azimuth to {target1:.1f}° (Original +45°)...")
    resp = send(f'AZ{target1:.0f}', delay=1.0)
    print(f"  Response: {resp}")

    # Poll for 3 seconds while it's moving
    print("  Polling position for 3 seconds...")
    start_time = time.time()
    while time.time() - start_time < 3:
        time.sleep(0.5)
        resp = send('AZ', delay=0.3)
        cur_az = parse_az(resp)
        if cur_az is not None:
            elapsed = time.time() - start_time
            print(f"    Az={cur_az:.1f}° [{elapsed:.1f}s]")

    # Step 2: After 3 seconds, send reversal command (Original - 25°)
    target2 = original_az - 25.0
    if target2 < 0.0:
        target2 += 360.0
    print(f"\n[Step 2] Sending reversal command: Azimuth to {target2:.1f}° (Original -25°)")
    print("  (This should trigger direction reversal safeguard)")
    resp = send(f'AZ{target2:.0f}', delay=1.0)
    print(f"  Response: {resp}")

    # Poll and observe the reversal behavior
    print("\n  Observing reversal behavior...")
    print("  Expected: Motor stops, waits for delay (default 1s), then starts new direction")
    prev_az = None
    for i in range(30):
        time.sleep(1)
        resp = send('AZ', delay=0.5)
        cur_az = parse_az(resp)
        if cur_az is not None:
            change = ""
            if prev_az is not None:
                diff = cur_az - prev_az
                if diff > 0:
                    change = f"(CW +{diff:.1f}°)"
                elif diff < 0:
                    change = f"(CCW {diff:.1f}°)"
            print(f"    Az={cur_az:.1f}° [{i+1}s] {change}")
            prev_az = cur_az
            # Check if we're close to target
            diff_from_target = abs(cur_az - target2)
            if diff_from_target > 180:
                diff_from_target = 360 - diff_from_target
            if diff_from_target <= 1.0:
                print(f"\n  ✓ Reached target — Az={cur_az:.1f}°")
                break
        else:
            print(f"    (no response) [{i+1}s]")

    print("\nTest complete — press Btn1 on the device to exit PC mode.")
    ser.close()


if __name__ == '__main__':
    main()
