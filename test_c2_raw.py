#!/usr/bin/env python3
"""
Raw C2 test — shows exact bytes received.

Usage:
    python3 test_c2_raw.py <serial_port>
"""

import sys
import time
import argparse
import serial


def main():
    parser = argparse.ArgumentParser(description='Raw C2 test')
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

    print("Escaping any blocking menu...")
    ser.write(b'MODE:ROTATOR\r\n')
    time.sleep(1)
    while ser.in_waiting:
        ser.readline()
    time.sleep(0.5)

    # Send C2 and show raw bytes
    print("\nSending C2...\n")
    ser.reset_input_buffer()
    ser.write(b'C2\r\n')
    time.sleep(1)
    raw = ser.read(ser.in_waiting or 64)
    print(f"Raw bytes: {raw.hex(' ')}")
    print(f"ASCII: {repr(raw)}")
    print(f"Decoded: {raw.decode('ascii', errors='replace')}")

    # Also test bare C
    print("\nSending C...\n")
    ser.reset_input_buffer()
    ser.write(b'C\r\n')
    time.sleep(1)
    raw = ser.read(ser.in_waiting or 64)
    print(f"Raw bytes: {raw.hex(' ')}")
    print(f"ASCII: {repr(raw)}")
    print(f"Decoded: {raw.decode('ascii', errors='replace')}")

    ser.close()


if __name__ == '__main__':
    main()
