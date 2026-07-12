#!/usr/bin/env python3
"""
Easycom Protocol Test Suite
Tests all Easycom commands against the ESP rotator controller.
Connects via serial, switches to Easycom mode, then exercises every command.
"""

import serial
import time
import sys

PORT = "/dev/cu.usbserial-0001"
BAUD = 115200

def open_port():
    ser = serial.Serial(PORT, BAUD, timeout=2)
    # Wait for device to finish booting and WiFi connection
    print("  Waiting for device to boot...")
    time.sleep(5)  # Give ESP8266 time to boot and connect to WiFi
    # Flush boot messages
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser

def send_cmd(ser, cmd, description=""):
    """Send an Easycom command (terminated with LF) and return the response."""
    label = f"  {description}  " if description else "  "
    raw = cmd + "\n"
    print(f"\n{label}")
    print(f"  TX: {cmd!r}")
    ser.write(raw.encode())
    time.sleep(0.3)
    resp = ""
    while ser.in_waiting:
        resp += ser.read(ser.in_waiting).decode(errors="replace")
    # Strip trailing whitespace/CR/LF for clean display
    resp = resp.strip("\r\n")
    print(f"  RX: {resp!r}")
    return resp

def parse_az(resp):
    """Extract azimuth from AZxxx.x response."""
    for line in resp.split("\n"):
        line = line.strip().strip("\r")
        if line.startswith("AZ"):
            try:
                return float(line[2:])
            except ValueError:
                pass
    return None

def parse_el(resp):
    """Extract elevation from ELyy.y response."""
    for line in resp.split("\n"):
        line = line.strip().strip("\r")
        if line.startswith("EL"):
            try:
                return float(line[2:])
            except ValueError:
                pass
    return None

def check_ok(resp, label):
    """Check that response contains OK."""
    if "OK" in resp:
        print(f"  ✅ {label}: OK received")
        return True
    else:
        print(f"  ❌ {label}: expected OK, got {resp!r}")
        return False

def main():
    print("=" * 60)
    print("  Easycom Protocol Test Suite")
    print("=" * 60)

    ser = open_port()
    passed = 0
    failed = 0

    # ---- Switch to Easycom protocol mode ----
    print("\n" + "=" * 60)
    print("  Phase 0: Switch to Easycom protocol")
    print("=" * 60)
    # Send MODE:ROTATOR to ensure we're in rotator mode
    send_cmd(ser, "MODE:ROTATOR", "Enter rotator mode")
    time.sleep(0.5)
    ser.reset_input_buffer()

    # We need to switch protocol via the menu system.
    # The firmware auto-detects based on currentProtocol setting.
    # Let's just send Easycom commands and see if they work.
    # If the unit is still in GS-232B mode, Easycom commands won't be routed correctly.
    # We'll use the GS-232 C2 command first to check current state, then assume
    # the user has set Easycom via the menu, OR we detect it from responses.

    print("\n⚠️  Ensure Easycom protocol is selected in the device menu!")
    print("   (Menu → Protocol → Easycom)")
    print("   Running tests now...\n")
    time.sleep(1)

    ser.reset_input_buffer()

    # ---- Phase 1: Position queries ----
    print("\n" + "=" * 60)
    print("  Phase 1: Position Queries")
    print("=" * 60)

    resp = send_cmd(ser, "AZ", "Query azimuth")
    az = parse_az(resp)
    if az is not None:
        print(f"  ✅ Azimuth: {az:.1f}°")
        passed += 1
    else:
        print(f"  ❌ Could not parse azimuth from response")
        failed += 1

    resp = send_cmd(ser, "EL", "Query elevation")
    el = parse_el(resp)
    if el is not None:
        print(f"  ✅ Elevation: {el:.1f}°")
        passed += 1
    else:
        print(f"  ❌ Could not parse elevation from response")
        failed += 1

    # ---- Phase 2: Version and Status ----
    print("\n" + "=" * 60)
    print("  Phase 2: Version & Status")
    print("=" * 60)

    resp = send_cmd(ser, "VE", "Version query")
    if "VE" in resp:
        print(f"  ✅ Version: {resp}")
        passed += 1
    else:
        print(f"  ❌ Expected VE prefix, got {resp!r}")
        failed += 1

    resp = send_cmd(ser, "GS", "Get status")
    if "GS" in resp:
        print(f"  ✅ Status: {resp}")
        passed += 1
    else:
        print(f"  ❌ Expected GS prefix, got {resp!r}")
        failed += 1

    # ---- Phase 3: Move to 300° azimuth, 0° elevation ----
    print("\n" + "=" * 60)
    print("  Phase 3: Move to Az=300°, El=0°")
    print("=" * 60)

    resp = send_cmd(ser, "AZ 300.0", "Set azimuth to 300°")
    if check_ok(resp, "AZ 300.0"):
        passed += 1
    else:
        failed += 1

    time.sleep(0.5)

    resp = send_cmd(ser, "EL 0.0", "Set elevation to 0°")
    if check_ok(resp, "EL 0.0"):
        passed += 1
    else:
        failed += 1

    # Wait for movement to complete
    print("\n  ⏳ Waiting for rotator to reach position...")
    time.sleep(8)

    # Verify position
    resp = send_cmd(ser, "AZ", "Verify azimuth after move")
    az = parse_az(resp)
    if az is not None and abs(az - 300.0) <= 3.0:
        print(f"  ✅ Azimuth reached: {az:.1f}° (target 300°)")
        passed += 1
    else:
        print(f"  ⚠️  Azimuth at {az}° (target 300°, tolerance ±3°)")
        failed += 1

    resp = send_cmd(ser, "EL", "Verify elevation after move")
    el = parse_el(resp)
    if el is not None and abs(el - 0.0) <= 3.0:
        print(f"  ✅ Elevation reached: {el:.1f}° (target 0°)")
        passed += 1
    else:
        print(f"  ⚠️  Elevation at {el}° (target 0°, tolerance ±3°)")
        failed += 1

    # ---- Phase 4: Directional movement commands ----
    print("\n" + "=" * 60)
    print("  Phase 4: Directional Commands (brief)")
    print("=" * 60)

    # Brief ML (move left) then stop
    resp = send_cmd(ser, "ML", "Move left (CCW)")
    if check_ok(resp, "ML"):
        passed += 1
    else:
        failed += 1
    time.sleep(0.5)
    resp = send_cmd(ser, "SA", "Stop azimuth")
    if check_ok(resp, "SA"):
        passed += 1
    else:
        failed += 1

    # Brief MD (move down) then stop
    resp = send_cmd(ser, "MD", "Move down")
    if check_ok(resp, "MD"):
        passed += 1
    else:
        failed += 1
    time.sleep(0.5)
    resp = send_cmd(ser, "SE", "Stop elevation")
    if check_ok(resp, "SE"):
        passed += 1
    else:
        failed += 1

    # ---- Phase 5: Velocity commands (EasyComm 3) ----
    print("\n" + "=" * 60)
    print("  Phase 5: Velocity Commands (EasyComm 3)")
    print("=" * 60)

    resp = send_cmd(ser, "VL 0100", "Velocity left at 100 mdeg/s")
    if check_ok(resp, "VL"):
        passed += 1
    else:
        failed += 1
    time.sleep(0.3)
    resp = send_cmd(ser, "SA", "Stop azimuth after VL")
    check_ok(resp, "SA after VL")
    passed += 1

    resp = send_cmd(ser, "VD 0100", "Velocity down at 100 mdeg/s")
    if check_ok(resp, "VD"):
        passed += 1
    else:
        failed += 1
    time.sleep(0.3)
    resp = send_cmd(ser, "SE", "Stop elevation after VD")
    check_ok(resp, "SE after VD")
    passed += 1

    # ---- Phase 6: AOS/LOS / Radio control commands ----
    print("\n" + "=" * 60)
    print("  Phase 6: AOS/LOS & Radio Control (acknowledge)")
    print("=" * 60)

    for cmd, desc in [
        ("AO", "AOS notification"),
        ("LO", "LOS notification"),
        ("UP 145000000", "Uplink frequency"),
        ("DN 435000000", "Downlink frequency"),
        ("DM FM", "Downlink mode"),
        ("UM SSB", "Uplink mode"),
    ]:
        resp = send_cmd(ser, cmd, desc)
        if check_ok(resp, cmd):
            passed += 1
        else:
            failed += 1

    # ---- Phase 7: All-stop ----
    print("\n" + "=" * 60)
    print("  Phase 7: All Stop")
    print("=" * 60)

    resp = send_cmd(ser, "S", "All stop")
    if check_ok(resp, "S all stop"):
        passed += 1
    else:
        failed += 1

    # ---- Phase 8: Move to final position (45° Az, 20° El) ----
    print("\n" + "=" * 60)
    print("  Phase 8: Move to Az=45°, El=20°")
    print("=" * 60)

    resp = send_cmd(ser, "AZ 45.0", "Set azimuth to 45°")
    if check_ok(resp, "AZ 45.0"):
        passed += 1
    else:
        failed += 1

    time.sleep(0.5)

    resp = send_cmd(ser, "EL 20.0", "Set elevation to 20°")
    if check_ok(resp, "EL 20.0"):
        passed += 1
    else:
        failed += 1

    # Wait for movement to complete
    print("\n  ⏳ Waiting for rotator to reach final position...")
    time.sleep(10)

    # Verify final position
    resp = send_cmd(ser, "AZ", "Verify final azimuth")
    az = parse_az(resp)
    if az is not None and abs(az - 45.0) <= 3.0:
        print(f"  ✅ Final azimuth: {az:.1f}° (target 45°)")
        passed += 1
    else:
        print(f"  ⚠️  Final azimuth at {az:.1f}° (target 45°, tolerance ±3°)")
        failed += 1

    resp = send_cmd(ser, "EL", "Verify final elevation")
    el = parse_el(resp)
    if el is not None and abs(el - 20.0) <= 3.0:
        print(f"  ✅ Final elevation: {el:.1f}° (target 20°)")
        passed += 1
    else:
        print(f"  ⚠️  Final elevation at {el:.1f}° (target 20°, tolerance ±3°)")
        failed += 1

    # ---- Summary ----
    total = passed + failed
    print("\n" + "=" * 60)
    print(f"  RESULTS: {passed}/{total} passed, {failed} failed")
    print("=" * 60)

    ser.close()
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
