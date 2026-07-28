#!/usr/bin/env python3
"""Listen to serial debug output for 2 minutes, filtering noise."""
import serial
import time
from datetime import datetime

PORT = "/dev/cu.usbserial-0001"
BAUD = 115200
DURATION = 120  # seconds

# Lines we care about
INTEREST_PREFIXES = ('DBG:', 'CAL:', 'AUTO:', 'WIFI:', 'TCP:', 'SETUP:', 
                     'BTN:', 'MOV:', 'STOP:', 'EEPROM:', 'FAULT:')

print(f"Listening on {PORT} at {BAUD} baud for {DURATION}s...")
print(f"Start: {datetime.now().strftime('%H:%M:%S')}")
print("-" * 60)

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(2)  # Wait for device to settle after reset
    
    lines = []
    start = time.time()
    
    while True:
        elapsed = time.time() - start
        if elapsed >= DURATION:
            break
        
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='replace').strip()
            if line and any(line.startswith(p) for p in INTEREST_PREFIXES):
                ts = time.strftime('%H:%M:%S')
                elapsed_s = int(elapsed)
                print(f"[{ts}] +{elapsed_s}s: {line}")
                lines.append((elapsed_s, line))
        
        time.sleep(0.01)
    
    ser.close()
    
    print("\n" + "=" * 60)
    end = datetime.now().strftime('%H:%M:%S')
    print(f"Stop: {end}")
    print(f"Total interesting lines captured: {len(lines)}")
    print("=" * 60)
    
    # Summary by category
    categories = {}
    for _, l in lines:
        prefix = l.split(':')[0] if ':' in l else 'OTHER'
        categories[prefix] = categories.get(prefix, 0) + 1
    
    for cat, count in sorted(categories.items(), key=lambda x: -x[1]):
        print(f"  {cat}: {count}")
    
    # Show TCP command breakdown
    tcp_cmds = [l for _, l in lines if l.startswith('DBG:')]
    if tcp_cmds:
        print("\nTCP commands received:")
        cmd_counts = {}
        for l in tcp_cmds:
            # Extract the actual command from DBG: line
            parts = l.split(': ', 1)
            if len(parts) > 1:
                # e.g., "DBG: CMD: P 320 45" or "DBG: CONNECTED" or "DBG: DISCONNECTED"
                inner = parts[1].strip()
                cmd_counts[inner] = cmd_counts.get(inner, 0) + 1
        
        for cmd, count in sorted(cmd_counts.items(), key=lambda x: -x[1]):
            print(f"  {cmd}: {count}")

except Exception as e:
    print(f"Error: {e}")
