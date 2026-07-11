#!/usr/bin/env python3
"""
Quick C/C1/C2/C3 command test.

Usage:
    python3 test_c2.py <serial_port>
"""

import sys
import time
import argparse
import serial


def main():
    parser = argparse.ArgumentParser(description='Test C/C1/C2/C3 commands')
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

    # Test all C command variants
    commands = ['C', 'C1', 'C2', 'C3', 'c', 'c1', 'c2', 'c3']
    print("\nTesting C command family:\n")
    for cmd in commands:
        resp = send(cmd, delay=1.0)
        status = "✓" if resp.startswith('C') else "✗"
        print(f"  {status}  Send: {cmd:4s}  Response: {resp}")

    # Also test H and V (GS-232A position queries)
    print("\nTesting GS-232A position queries:\n")
    for cmd in ['H', 'V']:
        resp = send(cmd, delay=1.0)
        status = "✓" if resp.startswith(cmd) else "✗"
        print(f"  {status}  Send: {cmd:4s}  Response: {resp}")

    # Test AZEL
    print("\nTesting AZEL query:\n")
    resp = send('AZEL', delay=1.0)
    status = "✓" if resp.startswith('AZEL') else "✗"
    print(f"  {status}  Send: AZEL    Response: {resp}")

    ser.close()


if __name__ == '__main__':
    main()
