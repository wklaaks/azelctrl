#!/usr/bin/env python3
"""
Systematic test for M command bug: M164 from azimuth 270° doesn't move.
Traces every decision point in executeProtocolAzMove().
"""
import serial
import time
import sys

PORT = "/dev/cu.usbserial-0001"
BAUD = 115200

def send_cmd(ser, cmd):
    """Send a command and wait for response."""
    ser.write((cmd + "\r").encode())
    time.sleep(0.1)
    
def read_response(ser, timeout=0.5):
    """Read all available responses."""
    ser.timeout = timeout
    data = b""
    while True:
        chunk = ser.read_all()
        if not chunk:
            break
        data += chunk
    ser.timeout = None
    return data.decode("utf-8", errors="replace")

def parse_c2(response):
    """Parse +nnnn+nnnn response."""
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

def test_scenario(ser, name, start_az, target_az):
    """Test a specific M command scenario."""
    print(f"\n{'='*60}")
    print(f"TEST: {name}")
    print(f"  Start azimuth: {start_az}°")
    print(f"  Target azimuth: {target_az}°")
    print(f"{'='*60}")
    
    # Get current position
    send_cmd(ser, "C2")
    resp = read_response(ser)
    az, el = parse_c2(resp)
    print(f"Current position: AZ={az}, EL={el}")
    
    if az is None:
        print("ERROR: Failed to read position")
        return False
    
    # Send M command
    cmd = f"M{target_az:03d}"
    print(f"\nSending: {cmd}")
    send_cmd(ser, cmd)
    resp = read_response(ser)
    print(f"Response: {resp.strip()}")
    
    # Monitor for movement and debug output
    print(f"\nMonitoring for 10 seconds...")
    started_moving = False
    stopped_at = None
    start_time = time.time()
    
    while time.time() - start_time < 10:
        send_cmd(ser, "C2")
        resp = read_response(ser)
        
        # Parse position
        new_az, new_el = parse_c2(resp)
        
        # Print debug lines
        for line in resp.split("\n"):
            line = line.strip("\r")
            if line.startswith("DBG:"):
                elapsed = time.time() - start_time
                print(f"[{elapsed:6.2f}s] {line}")
        
        # Check if moved
        if new_az is not None:
            elapsed = time.time() - start_time
            diff = target_az - new_az
            
            if abs(new_az - start_az) > 1:
                started_moving = True
                status = "MOVING"
                if abs(diff) <= 1.5:
                    stopped_at = new_az
                    status = "STOPPED"
            else:
                status = "NOT MOVED"
            
            if elapsed > 0.5 and int(elapsed * 2) % 2 == 0:  # Print every ~1s
                print(f"[{elapsed:6.2f}s] AZ={new_az:3d} EL={new_el:3d} diff={diff:+4.1f} [{status}]")
        
        time.sleep(0.25)
    
    print(f"\n{'='*60}")
    if started_moving:
        if stopped_at is not None:
            print(f"RESULT: SUCCESS - Rotator stopped at {stopped_at}° (target was {target_az}°)")
        else:
            print(f"RESULT: PARTIAL - Rotator moved but didn't stop at target")
    else:
        print(f"RESULT: FAILED - Rotator did NOT move from {start_az}°")
    print(f"{'='*60}\n")
    
    return started_moving

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    
    print("Opening serial port...")
    ser = serial.Serial(port, BAUD, timeout=1)
    time.sleep(2)  # Wait for ESP to reset
    
    ser.reset_input_buffer()
    time.sleep(0.5)
    
    print("Serial port open. Press Ctrl+C to stop.\n")
    
    try:
        # Test scenarios
        results = []
        
        # Scenario 1: M164 from 270° (the bug case)
        results.append(test_scenario(ser, "M164 from 270°", 270, 164))
        
        # Scenario 2: M164 from 90° (should work)
        results.append(test_scenario(ser, "M164 from 90°", 90, 164))
        
        # Scenario 3: M270 from 180° (simple CCW)
        results.append(test_scenario(ser, "M270 from 180°", 180, 270))
        
        # Scenario 4: M270 from 90° (simple CW)
        results.append(test_scenario(ser, "M270 from 90°", 90, 270))
        
        # Scenario 5: M010 from 350° (wrap through 0°)
        results.append(test_scenario(ser, "M010 from 350°", 350, 10))
        
        # Scenario 6: M350 from 010° (wrap through 0°)
        results.append(test_scenario(ser, "M350 from 010°", 10, 350))
        
        # Summary
        print(f"\n{'='*60}")
        print("SUMMARY")
        print(f"{'='*60}")
        for i, result in enumerate(results):
            status = "PASS" if result else "FAIL"
            print(f"Scenario {i+1}: {status}")
        print(f"{'='*60}\n")
        
    except KeyboardInterrupt:
        print("\n\nTest interrupted. Stopping rotator...")
        send_cmd(ser, "A")
        resp = read_response(ser)
        print(f"Response: {resp.strip()}")
    
    ser.close()

if __name__ == "__main__":
    main()
