#!/usr/bin/env python3
"""
Yaesu Rotator Controller - GS-232A/B & Easycom Protocol Tests

Usage:
    python3 test_protocol.py <serial_port>              # Unit tests (fast)
    python3 test_protocol.py <serial_port> --demo       # Slow movement demo (watchable)
    python3 test_protocol.py /dev/cu.usbserial-0001

The script sends commands to the ESP8266 via USB serial and verifies responses.
Debug output from the ESP8266 is filtered using the 'DBG:' prefix.
If the device is in menu mode, it will attempt to switch to rotator control mode.
"""

import sys
import time
import argparse
import serial
import serial.tools.list_ports
import re
from typing import Optional, List, Tuple


# ==================== SERIAL PORT DETECTION ====================

def find_esp8266_port() -> Optional[str]:
    """Auto-detect ESP8266 USB serial port."""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'usbserial' in port.device.lower() or 'esp' in port.device.lower():
            return port.device
    return None


def list_ports() -> List[str]:
    """List available serial ports."""
    ports = serial.tools.list_ports.comports()
    return [p.device for p in ports]


# ==================== PROTOCOL TESTER ====================

class ProtocolTester:
    """Test harness for GS-232A/B and Easycom protocols."""
    
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 2.0):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None
        self.results = []
        self.debug_lines = []
        
    def connect(self):
        """Open serial connection."""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            # Clear any buffered data
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            # Wait for device to settle and flush ALL boot messages
            time.sleep(5)
            # Flush all remaining boot output
            while self.ser.in_waiting:
                self.ser.readline()
            time.sleep(1)
            print(f"Connected to {self.port} at {self.baudrate} baud")
        except serial.SerialException as e:
            print(f"Failed to connect to {self.port}: {e}")
            sys.exit(1)
    
    def disconnect(self):
        """Close serial connection."""
        if self.ser and self.ser.is_open:
            self.ser.close()
    
    def send_command(self, cmd: str, delay: float = 0.2) -> str:
        """Send a command and return the response (stripped)."""
        if not self.ser or not self.ser.is_open:
            return "ERROR: Not connected"
        
        # Flush any stale data before sending
        self.ser.reset_input_buffer()
        time.sleep(0.2)
        
        # Send command with CRLF (protocol standard)
        self.ser.write((cmd + '\r\n').encode('ascii'))
        time.sleep(delay)
        
        # Read all available responses, return the last non-debug/non-protocol line
        response = ""
        while self.ser.in_waiting:
            line = self.ser.readline().decode('ascii', errors='replace').strip()
            # Filter out debug lines (prefixed with 'DBG:')
            if line.startswith('DBG:'):
                self.debug_lines.append(line)
                continue
            # Skip boot/status messages
            skip_prefixes = ('ROT:', 'BOOT:', 'SETUP:', 'Action:', 'WiFi', 'AP Mode', 
                           'Yaesu', 'Rotator Ctrl', 'MODE:', 'Rotator Control')
            if any(line.startswith(p) for p in skip_prefixes):
                continue
            if line:
                response = line  # Keep updating until we get the last line
        
        return response
    
    def assert_response(self, cmd: str, response: str, expected_pattern: str,
                        test_name: str = "") -> bool:
        """Check if response matches expected pattern."""
        passed = expected_pattern in response
        status = "PASS" if passed else "FAIL"
        result = {
            'test': test_name or cmd,
            'cmd': cmd,
            'response': response,
            'expected': expected_pattern,
            'passed': passed
        }
        self.results.append(result)
        print(f"  [{status}] {test_name or cmd}: got '{response}' (expected '{expected_pattern}')")
        return passed
    
    def assert_response_pattern(self, cmd: str, response: str, pattern_func,
                                 test_name: str = "") -> bool:
        """Check if response matches a custom pattern function."""
        passed = pattern_func(response)
        status = "PASS" if passed else "FAIL"
        result = {
            'test': test_name or cmd,
            'cmd': cmd,
            'response': response,
            'expected': str(pattern_func),
            'passed': passed
        }
        self.results.append(result)
        print(f"  [{status}] {test_name or cmd}: got '{response}'")
        return passed
    
    def print_summary(self):
        """Print test summary."""
        total = len(self.results)
        passed = sum(1 for r in self.results if r['passed'])
        failed = total - passed
        print(f"\n{'='*60}")
        print(f"Test Summary: {passed}/{total} passed, {failed} failed")
        print(f"{'='*60}")
        if failed > 0:
            print("\nFailed tests:")
            for r in self.results:
                if not r['passed']:
                    print(f"  - {r['test']}: expected '{r['expected']}', got '{r['response']}'")
        if self.debug_lines:
            print(f"\nDebug output ({len(self.debug_lines)} lines):")
            for line in self.debug_lines[:10]:
                print(f"  {line}")
            if len(self.debug_lines) > 10:
                print(f"  ... and {len(self.debug_lines) - 10} more")
        return failed == 0


# ==================== GS-232A/B TEST CASES ====================

def test_gs232_heading(tester: ProtocolTester) -> bool:
    """Test GS-232A/B H command (heading/azimuth)."""
    print("\n--- GS-232A/B Heading Test ---")
    response = tester.send_command('H')
    return tester.assert_response('H', response, 'H', 'Heading')


def test_gs232_vertical(tester: ProtocolTester) -> bool:
    """Test GS-232A/B V command (vertical/elevation)."""
    print("\n--- GS-232A/B Vertical Test ---")
    response = tester.send_command('V')
    return tester.assert_response('V', response, 'V', 'Vertical')


def test_gs232_combined(tester: ProtocolTester) -> bool:
    """Test GS-232A/B C command (combined azimuth/elevation)."""
    print("\n--- GS-232A/B Combined Test ---")
    response = tester.send_command('C')
    return tester.assert_response('C', response, 'C', 'Combined')


def test_gs232_stop(tester: ProtocolTester) -> bool:
    """Test GS-232A/B S command (stop)."""
    print("\n--- GS-232A/B Stop Test ---")
    response = tester.send_command('S')
    return tester.assert_response('S', response, 'OK', 'Stop')


def test_gs232_left(tester: ProtocolTester) -> bool:
    """Test GS-232A/B L command (rotate left)."""
    print("\n--- GS-232A/B Left Test ---")
    response = tester.send_command('L')
    return tester.assert_response('L', response, 'OK', 'Left')


def test_gs232_right(tester: ProtocolTester) -> bool:
    """Test GS-232A/B R command (rotate right)."""
    print("\n--- GS-232A/B Right Test ---")
    response = tester.send_command('R')
    return tester.assert_response('R', response, 'OK', 'Right')


def test_gs232_up(tester: ProtocolTester) -> bool:
    """Test GS-232A/B U command (elevation up)."""
    print("\n--- GS-232A/B Up Test ---")
    response = tester.send_command('U')
    return tester.assert_response('U', response, 'OK', 'Up')


def test_gs232_down(tester: ProtocolTester) -> bool:
    """Test GS-232A/B D command (elevation down)."""
    print("\n--- GS-232A/B Down Test ---")
    response = tester.send_command('D')
    return tester.assert_response('D', response, 'OK', 'Down')


def test_gs232_speed(tester: ProtocolTester) -> bool:
    """Test GS-232A/B X command (speed control)."""
    print("\n--- GS-232A/B Speed Test ---")
    response = tester.send_command('X128')
    return tester.assert_response('X128', response, 'OK', 'Speed')


def test_gs232_timeout(tester: ProtocolTester) -> bool:
    """Test GS-232A/B T command (timeout)."""
    print("\n--- GS-232A/B Timeout Test ---")
    response = tester.send_command('T10')
    return tester.assert_response('T10', response, 'OK', 'Timeout')


def test_gs232_park(tester: ProtocolTester) -> bool:
    """Test GS-232A/B P command (park)."""
    print("\n--- GS-232A/B Park Test ---")
    response = tester.send_command('P')
    return tester.assert_response('P', response, 'OK', 'Park')


def test_gs232_north_center(tester: ProtocolTester) -> bool:
    """Test GS-232A/B N command (north center)."""
    print("\n--- GS-232A/B North Center Test ---")
    response = tester.send_command('N')
    return tester.assert_response('N', response, 'OK', 'North Center')


def test_gs232_south_center(tester: ProtocolTester) -> bool:
    """Test GS-232A/B F command (south center)."""
    print("\n--- GS-232A/B South Center Test ---")
    response = tester.send_command('F')
    return tester.assert_response('F', response, 'OK', 'South Center')


def test_gs232_case_insensitive(tester: ProtocolTester) -> bool:
    """Test GS-232A/B case insensitivity."""
    print("\n--- GS-232A/B Case Insensitive Test ---")
    response = tester.send_command('h')
    return tester.assert_response('h', response, 'H', 'Case Insensitive')


def test_gs232_unknown(tester: ProtocolTester) -> bool:
    """Test GS-232A/B unknown command."""
    print("\n--- GS-232A/B Unknown Command Test ---")
    response = tester.send_command('Z')
    return tester.assert_response('Z', response, 'ERR', 'Unknown Command')


# ==================== EASYCOM TEST CASES ====================

def test_easycom_azimuth_query(tester: ProtocolTester) -> bool:
    """Test Easycom AZ command (azimuth query)."""
    print("\n--- Easycom Azimuth Query Test ---")
    response = tester.send_command('AZ')
    return tester.assert_response('AZ', response, 'AZ', 'Azimuth Query')


def test_easycom_elevation_query(tester: ProtocolTester) -> bool:
    """Test Easycom EL command (elevation query)."""
    print("\n--- Easycom Elevation Query Test ---")
    response = tester.send_command('EL')
    return tester.assert_response('EL', response, 'EL', 'Elevation Query')


def test_easycom_azimuth_move(tester: ProtocolTester) -> bool:
    """Test Easycom AZnnn command (move to azimuth)."""
    print("\n--- Easycom Azimuth Move Test ---")
    response = tester.send_command('AZ180')
    return tester.assert_response('AZ180', response, 'OK', 'Azimuth Move')


def test_easycom_elevation_move(tester: ProtocolTester) -> bool:
    """Test Easycom ELnnn command (move to elevation)."""
    print("\n--- Easycom Elevation Move Test ---")
    response = tester.send_command('EL45')
    return tester.assert_response('EL45', response, 'OK', 'Elevation Move')


def test_easycom_combined_query(tester: ProtocolTester) -> bool:
    """Test Easycom AZEL command (combined query)."""
    print("\n--- Easycom Combined Query Test ---")
    response = tester.send_command('AZEL')
    return tester.assert_response('AZEL', response, 'AZEL', 'Combined Query')


def test_easycom_cw(tester: ProtocolTester) -> bool:
    """Test Easycom CW command."""
    print("\n--- Easycom CW Test ---")
    response = tester.send_command('CW')
    return tester.assert_response('CW', response, 'OK', 'CW')


def test_easycom_ccw(tester: ProtocolTester) -> bool:
    """Test Easycom CCW command."""
    print("\n--- Easycom CCW Test ---")
    response = tester.send_command('CCW')
    return tester.assert_response('CCW', response, 'OK', 'CCW')


def test_easycom_up(tester: ProtocolTester) -> bool:
    """Test Easycom UP command."""
    print("\n--- Easycom UP Test ---")
    response = tester.send_command('UP')
    return tester.assert_response('UP', response, 'OK', 'UP')


def test_easycom_down(tester: ProtocolTester) -> bool:
    """Test Easycom DOWN command."""
    print("\n--- Easycom DOWN Test ---")
    response = tester.send_command('DOWN')
    return tester.assert_response('DOWN', response, 'OK', 'DOWN')


def test_easycom_stop(tester: ProtocolTester) -> bool:
    """Test Easycom STOP command."""
    print("\n--- Easycom STOP Test ---")
    response = tester.send_command('STOP')
    return tester.assert_response('STOP', response, 'OK', 'STOP')


def test_easycom_timeout(tester: ProtocolTester) -> bool:
    """Test Easycom TIMEOUT command."""
    print("\n--- Easycom TIMEOUT Test ---")
    response = tester.send_command('TIMEOUT10')
    return tester.assert_response('TIMEOUT10', response, 'OK', 'TIMEOUT')


def test_easycom_speed(tester: ProtocolTester) -> bool:
    """Test Easycom SPEED command."""
    print("\n--- Easycom SPEED Test ---")
    response = tester.send_command('SPEED128')
    return tester.assert_response('SPEED128', response, 'OK', 'SPEED')


def test_easycom_park(tester: ProtocolTester) -> bool:
    """Test Easycom PARK command."""
    print("\n--- Easycom PARK Test ---")
    response = tester.send_command('PARK')
    return tester.assert_response('PARK', response, 'OK', 'PARK')


def test_easycom_help(tester: ProtocolTester) -> bool:
    """Test Easycom HELP command."""
    print("\n--- Easycom HELP Test ---")
    response = tester.send_command('HELP')
    return tester.assert_response('HELP', response, 'HELP', 'HELP')


def test_easycom_backslash_help(tester: ProtocolTester) -> bool:
    """Test Easycom backslash help command."""
    print("\n--- Easycom Backslash Help Test ---")
    response = tester.send_command('\\?')
    return tester.assert_response('\\?', response, '\\?', 'Backslash Help')


def test_easycom_unknown(tester: ProtocolTester) -> bool:
    """Test Easycom unknown command."""
    print("\n--- Easycom Unknown Command Test ---")
    response = tester.send_command('UNKNOWN')
    return tester.assert_response('UNKNOWN', response, 'ERR', 'Unknown Command')


# ==================== SLOW DEMO TEST ====================
# Reads start position, moves one axis at a time (5° away, back),
# then does a combined movement (both axes), waits 5s, then returns.

def parse_az(response: str) -> Optional[float]:
    """Extract azimuth from AZnnn.n or AZEL response."""
    # Handle combined AZEL format: AZEL355.0.20.2.0
    if response.startswith('AZEL'):
        m = re.match(r'AZEL(\d+\.\d+)', response)
        if m:
            return float(m.group(1))
    # Handle standalone AZ format: AZ355.0
    m = re.match(r'AZ(\d+\.\d+)', response)
    if m:
        return float(m.group(1))
    return None

def parse_el(response: str) -> Optional[float]:
    """Extract elevation from ELnnn.n or AZEL response."""
    # Handle combined AZEL format: AZEL355.0.20.2.0
    if response.startswith('AZEL'):
        m = re.match(r'AZEL\d+\.\d+\.(\d+\.\d+)', response)
        if m:
            return float(m.group(1))
    # Handle standalone EL format: EL20.2
    m = re.match(r'EL(\d+\.\d+)', response)
    if m:
        return float(m.group(1))
    return None

def safe_azimuth(az: float, offset: float) -> float:
    """Clamp azimuth to 0-360 range."""
    return max(0.0, min(360.0, az + offset))

def safe_elevation(el: float, offset: float) -> float:
    """Clamp elevation to 0-180 range."""
    return max(0.0, min(180.0, el + offset))

def run_demo(tester: ProtocolTester):
    """Slow movement demo — watch the OLED while this runs."""
    print("\n" + "="*60)
    print("SLOW MOVEMENT DEMO")
    print("Watch the OLED for RX/TX command tracking")
    print("="*60)
    
    DELAY = 1.5  # Pause between commands so user can see OLED updates
    
    # --- Read start position ---
    print(f"\n[Step 1] Reading start position...")
    time.sleep(DELAY)
    resp = tester.send_command('AZEL', delay=DELAY)
    print(f"  Response: {resp}")
    start_az = parse_az(resp)
    start_el = parse_el(resp)
    if start_az is None or start_el is None:
        print("  ERROR: Could not parse start position. Aborting demo.")
        return
    print(f"  Start: Az={start_az:.1f}°, El={start_el:.1f}°")
    time.sleep(DELAY)
    
    # --- Test A: Azimuth only (5° away, then back) ---
    target_az = safe_azimuth(start_az, 5.0)
    print(f"\n[Step 2] AZIMUTH ONLY — moving to Az={target_az:.1f}° (5° from start)")
    print(f"  Sending: AZ{target_az:.1f}")
    resp = tester.send_command(f'AZ{target_az:.1f}', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    
    # Verify position
    resp = tester.send_command('AZ', delay=DELAY)
    print(f"  Verify AZ: {resp}")
    time.sleep(DELAY)
    
    # Move back
    print(f"\n[Step 3] AZIMUTH ONLY — returning to Az={start_az:.1f}°")
    print(f"  Sending: AZ{start_az:.1f}")
    resp = tester.send_command(f'AZ{start_az:.1f}', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    
    # Verify position
    resp = tester.send_command('AZ', delay=DELAY)
    print(f"  Verify AZ: {resp}")
    time.sleep(DELAY)
    
    # --- Test B: Elevation only (5° away, then back) ---
    target_el = safe_elevation(start_el, 5.0)
    print(f"\n[Step 4] ELEVATION ONLY — moving to El={target_el:.1f}° (5° from start)")
    print(f"  Sending: EL{target_el:.1f}")
    resp = tester.send_command(f'EL{target_el:.1f}', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    
    # Verify position
    resp = tester.send_command('EL', delay=DELAY)
    print(f"  Verify EL: {resp}")
    time.sleep(DELAY)
    
    # Move back
    print(f"\n[Step 5] ELEVATION ONLY — returning to El={start_el:.1f}°")
    print(f"  Sending: EL{start_el:.1f}")
    resp = tester.send_command(f'EL{start_el:.1f}', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    
    # Verify position
    resp = tester.send_command('EL', delay=DELAY)
    print(f"  Verify EL: {resp}")
    time.sleep(DELAY)
    
    # --- Test C: Combined movement (both axes, 5° each) ---
    target_az2 = safe_azimuth(start_az, 5.0)
    target_el2 = safe_elevation(start_el, 5.0)
    print(f"\n[Step 6] COMBINED — moving to Az={target_az2:.1f}°, El={target_el2:.1f}°")
    print(f"  Sending: AZ{target_az2:.1f}")
    resp = tester.send_command(f'AZ{target_az2:.1f}', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(DELAY)
    print(f"  Sending: EL{target_el2:.1f}")
    resp = tester.send_command(f'EL{target_el2:.1f}', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    
    # Wait 5 seconds at target
    print(f"\n[Step 7] Waiting 5 seconds at target position...")
    for i in range(5, 0, -1):
        print(f"  {i}...")
        time.sleep(1)
    
    # Verify position at target
    resp = tester.send_command('AZEL', delay=DELAY)
    print(f"  Verify AZEL at target: {resp}")
    time.sleep(DELAY)
    
    # Move back to start
    print(f"\n[Step 8] COMBINED — returning to Az={start_az:.1f}°, El={start_el:.1f}°")
    print(f"  Sending: AZ{start_az:.1f}")
    resp = tester.send_command(f'AZ{start_az:.1f}', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(DELAY)
    print(f"  Sending: EL{start_el:.1f}")
    resp = tester.send_command(f'EL{start_el:.1f}', delay=DELAY)
    print(f"  Response: {resp}")
    time.sleep(3)  # Let movement complete
    
    # Final verification
    print(f"\n[Step 9] Final verification...")
    resp = tester.send_command('AZEL', delay=DELAY)
    print(f"  Final AZEL: {resp}")
    final_az = parse_az(resp)
    final_el = parse_el(resp)
    if final_az is not None and final_el is not None:
        az_diff = abs(final_az - start_az)
        el_diff = abs(final_el - start_el)
        print(f"  Az diff: {az_diff:.1f}°, El diff: {el_diff:.1f}°")
        if az_diff < 1.0 and el_diff < 1.0:
            print("  ✓ Returned to start position within tolerance")
        else:
            print("  ⚠ Position differs from start — check rotator")
    
    print(f"\n{'='*60}")
    print("DEMO COMPLETE")
    print(f"{'='*60}")


# ==================== MAIN ====================

def main():
    parser = argparse.ArgumentParser(description='Yaesu Rotator Protocol Tester')
    parser.add_argument('port', nargs='?', default=None, help='Serial port (auto-detect if not specified)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--list', action='store_true', help='List available ports')
    parser.add_argument('--demo', action='store_true', help='Run slow movement demo (watch OLED for RX/TX tracking)')
    args = parser.parse_args()
    
    # List ports if requested
    if args.list:
        print("Available serial ports:")
        for port in list_ports():
            print(f"  {port}")
        return
    
    # Auto-detect port if not specified
    port = args.port
    if not port:
        port = find_esp8266_port()
        if not port:
            print("No ESP8266 port found. Use --list to see available ports.")
            sys.exit(1)
        print(f"Auto-detected port: {port}")
    
    # Create tester and connect
    tester = ProtocolTester(port, baudrate=args.baud)
    tester.connect()
    
    # Small delay to let device settle
    time.sleep(1)
    
    # Force device into rotator control mode (escapes any blocking menu)
    print("Sending MODE:ROTATOR to escape any blocking menu...")
    tester.send_command('MODE:ROTATOR', delay=0.5)
    time.sleep(0.5)
    
    if args.demo:
        # Run slow movement demo
        run_demo(tester)
    else:
        # Verify we got the response
        response = tester.send_command('H', delay=0.5)
        print(f"Device response to H: {response}")
        if not response.startswith('H'):
            print("WARNING: Device may not be in rotator control mode. Check device state.")
        
        # Run GS-232A/B tests
        print("\n" + "="*60)
        print("GS-232A/B Protocol Tests")
        print("="*60)
        gs232_tests = [
            test_gs232_heading,
            test_gs232_vertical,
            test_gs232_combined,
            test_gs232_stop,
            test_gs232_left,
            test_gs232_right,
            test_gs232_up,
            test_gs232_down,
            test_gs232_speed,
            test_gs232_timeout,
            test_gs232_park,
            test_gs232_north_center,
            test_gs232_south_center,
            test_gs232_case_insensitive,
            test_gs232_unknown,
        ]
        
        for test in gs232_tests:
            test(tester)
        
        # Run Easycom tests
        print("\n" + "="*60)
        print("Easycom Protocol Tests")
        print("="*60)
        easycom_tests = [
            test_easycom_azimuth_query,
            test_easycom_elevation_query,
            test_easycom_azimuth_move,
            test_easycom_elevation_move,
            test_easycom_combined_query,
            test_easycom_cw,
            test_easycom_ccw,
            test_easycom_up,
            test_easycom_down,
            test_easycom_stop,
            test_easycom_timeout,
            test_easycom_speed,
            test_easycom_park,
            test_easycom_help,
            test_easycom_backslash_help,
            test_easycom_unknown,
        ]
        
        for test in easycom_tests:
            test(tester)
        
        # Print summary
        tester.print_summary()
    
    tester.disconnect()


if __name__ == '__main__':
    main()
