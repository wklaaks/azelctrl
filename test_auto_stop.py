#!/usr/bin/env python3
"""
Test auto-stop with M command. Sends M030 and monitors debug output + C2 responses.
"""
import serial
import time
import sys

PORT = "/dev/cu.usbserial-0001"
BAUD = 115200

def send_cmd(s, cmd):
    s.write((cmd + "\r").encode())
    time.sleep(0.1)

def read_all(s, timeout=0.5):
    s.timeout = timeout
    data = b""
    while True:
        chunk = s.read_all()
        if not chunk:
            break
        data += chunk
    s.timeout = None
    return data.decode("utf-8", errors="replace")

def parse_c2(response):
    """Parse +nnnn+nnnn response"""
    for line in response.split("\n"):
        line = line.strip("\r")
        if line.startswith("+") and "+" in line[1:]:
            parts = line.split("+")
            if len(parts) >= 3:
                try:
                    az = int(parts[1])
                    el = int(parts[2])
                    return az, el
                except ValueError:
                    pass
    return None, None

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    target = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    
    print(f"Opening {port} at {BAUD} baud...")
    ser = serial.Serial(port, BAUD, timeout=1)
    time.sleep(2)  # Wait for ESP to reset
    
    # Clear buffer
    ser.reset_input_buffer()
    time.sleep(0.5)
    
    # Get current position
    print("Getting current position...")
    send_cmd(ser, "C2")
    resp = read_all(ser)
    az, el = parse_c2(resp)
    print(f"Current: AZ={az}, EL={el}")
    
    if az is None:
        print("Failed to read position. Raw response:")
        print(repr(resp))
        ser.close()
        return
    
    # Send M command
    print(f"\nSending M{target:03d} (target={target})...")
    send_cmd(ser, f"M{target:03d}")
    resp = read_all(ser)
    print(f"Response: {resp.strip()}")
    
    # Monitor movement
    print(f"\nMonitoring movement (target={target})...")
    print("Press Ctrl+C to stop\n")
    
    try:
        start = time.time()
        while time.time() - start < 30:  # Max 30 seconds
            send_cmd(ser, "C2")
            resp = read_all(ser)
            
            # Print debug lines
            for line in resp.split("\n"):
                line = line.strip("\r")
                if line.startswith("DBG:"):
                    elapsed = time.time() - start
                    print(f"[{elapsed:6.2f}s] {line}")
            
            # Parse position
            az, el = parse_c2(resp)
            if az is not None:
                elapsed = time.time() - start
                diff = target - az
                marker = " <-- STOPPED" if abs(diff) <= 1 else ""
                print(f"[{elapsed:6.2f}s] AZ={az:3d} EL={el:3d} diff={diff:+3d}{marker}")
            
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n\nStopping...")
        send_cmd(ser, "A")
        resp = read_all(ser)
        print(f"Response: {resp.strip()}")
    
    # Final position
    time.sleep(0.5)
    send_cmd(ser, "C2")
    resp = read_all(ser)
    az, el = parse_c2(resp)
    print(f"\nFinal position: AZ={az}, EL={el}")
    
    ser.close()

if __name__ == "__main__":
    main()
