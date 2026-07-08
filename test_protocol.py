#!/usr/bin/env python3
"""
Yaesu Rotator Controller - GS-232A/B & Easycom Protocol Unit Tests

Usage:
    python3 test_protocol.py <serial_port>
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
    
    def __init__(self, port: str, baudrate: int = 9600, timeout: float = 2.0):
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
            # Wait for device to settle and flush boot messages
            time.sleep(2)
            while self.ser.in_waiting:
                self.ser.readline()
            print(f"Connected to {self.port} at {self.baudrate} baud")
        except serial.SerialException as e:
            print(f"Failed to connect to {self.port}: {e}")
            sys.exit(1)
    
    def disconnect(self):
        """Close serial connection."""
        if self.ser and self.ser.is_open:
            self.ser.close()
    
    def send_command(self, cmd: str, delay: float = 0.1) -> str:
        """Send a command and return the response (stripped)."""
        if not self.ser or not self.ser.is_open:
            return "ERROR: Not connected"
        
        # Flush any stale data before sending
        self.ser.reset_input_buffer()
        
        # Send command with CRLF (protocol standard)
        self.ser.write((cmd + '\r\n').encode('ascii'))
        time.sleep(delay)
        
        # Read all available responses, return the last non-debug line
        response = ""
        while self.ser.in_waiting:
            line = self.ser.readline().decode('ascii', errors='replace').strip()
            # Filter out debug lines (prefixed with 'DBG:')
            if line.startswith('DBG:'):
                self.debug_lines.append(line)
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


# ==================== MAIN ====================

def main():
    parser = argparse.ArgumentParser(description='Yaesu Rotator Protocol Tester')
    parser.add_argument('port', nargs='?', default=None, help='Serial port (auto-detect if not specified)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--list', action='store_true', help='List available ports')
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
    
    # Create tester and run tests
    tester = ProtocolTester(port, baudrate=args.baud)
    tester.connect()
    
    # Small delay to let device settle
    time.sleep(1)
    
    # Force device into rotator control mode (escapes any blocking menu)
    print("Sending MODE:ROTATOR to escape any blocking menu...")
    tester.send_command('MODE:ROTATOR', delay=0.5)
    time.sleep(0.5)
    
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
