#!/usr/bin/env python3
"""
Yaesu Rotator Controller - Heading Test

Tests specific heading movements with active polling:
  1. Read start position
  2. AZ -> 25° (poll until reached + stopped)
  3. AZ -> 315° (poll until reached + stopped, wait 5s)
  4. AZ -> 185° (poll until reached + stopped, wait 5s)
  5. AZ -> 175° (poll until reached + stopped)
  6. EL -> 25° (poll until reached + stopped)

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


def wait_for_target(ser, target_az: Optional[float] = None, target_el: Optional[float] = None,
                    tolerance: float = 1.0, poll_interval: float = 0.5, timeout: float = 60.0):
    """
    Poll position until target is reached and motor has stopped.
    
    Returns (final_az, final_el) or None on timeout.
    """
    start_time = time.time()
    last_az = None
    last_el = None
    stable_count = 0
    required_stable = 3  # Number of consecutive stable readings to confirm stopped
    
    while time.time() - start_time < timeout:
        # Query position
        ser.reset_input_buffer()
        time.sleep(0.1)
        ser.write(b'AZEL\r\n')
        time.sleep(0.5)
        response = ""
        while ser.in_waiting:
            line = ser.readline().decode('ascii', errors='replace').strip()
            if line and not line.startswith(('DBG:', 'ROT:', 'BOOT:', 'SETUP:', 'Action:', 'WiFi')):
                response = line
                break
        
        if not response:
            time.sleep(poll_interval)
            continue
        
        az = parse_az(response)
        el = parse_el(response)
        
        if az is not None and el is not None:
            # Check if we're at target
            at_target = True
            if target_az is not None and az is not None:
                diff = abs(az - target_az)
                if diff > 180:
                    diff = 360 - diff
                if diff > tolerance:
                    at_target = False
            
            if target_el is not None and el is not None:
                if abs(el - target_el) > tolerance:
                    at_target = False
            
            # Check if position is stable (motor stopped)
            position_stable = False
            if last_az is not None and last_el is not None:
                if abs(az - last_az) < 0.5 and abs(el - last_el) < 0.5:
                    stable_count += 1
                    if stable_count >= required_stable:
                        position_stable = True
                else:
                    stable_count = 0
            
            if at_target and position_stable:
                print(f"  ✓ Target reached and stable: Az={az:.1f}°, El={el:.1f}°")
                return (az, el)
            
            last_az = az
            last_el = el
            
            # Show progress
            elapsed = time.time() - start_time
            if target_az is not None:
                az_diff = abs(az - target_az)
                if az_diff > 180:
                    az_diff = 360 - az_diff
                print(f"  Polling... Az={az:.1f}° (target {target_az:.1f}°, diff {az_diff:.1f}°) [{elapsed:.0f}s]")
            elif target_el is not None:
                el_diff = abs(el - target_el)
                print(f"  Polling... El={el:.1f}° (target {target_el:.1f}°, diff {el_diff:.1f}°) [{elapsed:.0f}s]")
        
        time.sleep(poll_interval)
    
    print(f"  ⚠ Timeout waiting for target after {timeout}s")
    return (last_az, last_el) if last_az is not None else None


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

    def send(cmd: str, delay: float = 0.5) -> str:
        """Send a command and return the response."""
        ser.reset_input_buffer()
        time.sleep(0.1)
        ser.write((cmd + '\r\n').encode('ascii'))
        time.sleep(delay)
        response = ""
        while ser.in_waiting:
            line = ser.readline().decode('ascii', errors='replace').strip()
            skip_prefixes = ('DBG:', 'ROT:', 'BOOT:', 'SETUP:', 'Action:', 'WiFi',
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

    DELAY = 0.5  # Pause between commands for OLED visibility

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
    result = wait_for_target(ser, target_az=25.0, target_el=None)
    if result:
        az, el = result
        print(f"  Final: Az={az:.1f}° (target 25.0°, diff={abs(az-25.0):.1f}°)")
    time.sleep(DELAY)

    # --- Step 3: AZ -> 315° (wait 5s) ---
    print(f"\n[Step 3] Moving AZ to 315.0°")
    resp = send('AZ315', delay=DELAY)
    print(f"  Response: {resp}")
    result = wait_for_target(ser, target_az=315.0, target_el=None)
    if result:
        az, el = result
        print(f"  Final: Az={az:.1f}° (target 315.0°, diff={abs(az-315.0):.1f}°)")
    print(f"  Waiting 5 seconds at heading...")
    for i in range(5, 0, -1):
        print(f"  {i}...")
        time.sleep(1)
    time.sleep(DELAY)

    # --- Step 4: AZ -> 185° (wait 5s) ---
    print(f"\n[Step 4] Moving AZ to 185.0°")
    resp = send('AZ185', delay=DELAY)
    print(f"  Response: {resp}")
    result = wait_for_target(ser, target_az=185.0, target_el=None)
    if result:
        az, el = result
        print(f"  Final: Az={az:.1f}° (target 185.0°, diff={abs(az-185.0):.1f}°)")
    print(f"  Waiting 5 seconds at heading...")
    for i in range(5, 0, -1):
        print(f"  {i}...")
        time.sleep(1)
    time.sleep(DELAY)

    # --- Step 5: AZ -> 175° ---
    print(f"\n[Step 5] Moving AZ to 175.0°")
    resp = send('AZ175', delay=DELAY)
    print(f"  Response: {resp}")
    result = wait_for_target(ser, target_az=175.0, target_el=None)
    if result:
        az, el = result
        print(f"  Final: Az={az:.1f}° (target 175.0°, diff={abs(az-175.0):.1f}°)")
    time.sleep(DELAY)

    # --- Step 6: EL -> 25° ---
    print(f"\n[Step 6] Moving EL to 25.0°")
    resp = send('EL25', delay=DELAY)
    print(f"  Response: {resp}")
    result = wait_for_target(ser, target_az=None, target_el=25.0)
    if result:
        az, el = result
        print(f"  Final: El={el:.1f}° (target 25.0°, diff={abs(el-25.0):.1f}°)")
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
