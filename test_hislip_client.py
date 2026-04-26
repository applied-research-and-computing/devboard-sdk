#!/usr/bin/env python3
"""
HiSLIP socket client test script for ESP32 instrument.
Tests basic connectivity and SCPI commands via raw TCP socket.
"""

import socket
import struct
import sys
import time
from typing import Tuple, Optional


class HiSLIPClient:
    """Simple HiSLIP protocol client for testing."""

    PROLOGUE = b"HS"
    MSG_TYPE_INITIALIZE = 0
    MSG_TYPE_DATA = 7

    def __init__(self, host: str, port: int = 4880, timeout: float = 2.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None

    def connect(self) -> bool:
        """Establish TCP connection and perform HiSLIP handshake."""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(self.timeout)
            print(f"[*] Connecting to {self.host}:{self.port}...")
            self.sock.connect((self.host, self.port))
            print("[+] TCP connection established")

            # Send Initialize message
            init_msg = struct.pack(">2sBBIQ",
                                   self.PROLOGUE,
                                   self.MSG_TYPE_INITIALIZE,
                                   0,  # Control byte
                                   0x00010000,  # Message parameter
                                   0)  # Payload length
            self.sock.send(init_msg)
            print("[*] Sent Initialize message")

            # Receive Initialize response
            response = self.sock.recv(1024)
            if response[:2] == self.PROLOGUE:
                print(f"[+] Initialize response received: {response.hex()}")
                return True
            else:
                print("[-] Invalid Initialize response")
                return False

        except socket.timeout:
            print("[-] Connection timeout")
            return False
        except ConnectionRefusedError:
            print(f"[-] Connection refused to {self.host}:{self.port}")
            return False
        except socket.gaierror:
            print(f"[-] Could not resolve hostname: {self.host}")
            return False
        except Exception as e:
            print(f"[-] Connection error: {e}")
            return False

    def send_scpi(self, command: str) -> Optional[bytes]:
        """Send SCPI command and receive response."""
        if not self.sock:
            print("[-] Not connected")
            return None

        try:
            cmd_bytes = (command + '\n').encode('utf-8')

            # Send DataEnd message (type 7)
            data_msg = struct.pack(">2sBBIQ",
                                   self.PROLOGUE,
                                   self.MSG_TYPE_DATA,
                                   0,  # Control byte
                                   0,  # Message parameter
                                   len(cmd_bytes))
            self.sock.send(data_msg + cmd_bytes)
            print(f"[*] Sent SCPI command: {command}")

            # Receive response
            response = self.sock.recv(1024)
            if response:
                # Skip HiSLIP header (14 bytes) to get payload
                payload = response[14:] if len(response) > 14 else response
                return payload
            else:
                print("[-] No response received")
                return None

        except socket.timeout:
            print("[-] Response timeout")
            return None
        except Exception as e:
            print(f"[-] Error sending command: {e}")
            return None

    def close(self):
        """Close TCP connection."""
        if self.sock:
            self.sock.close()
            print("[*] Connection closed")


def test_standard_commands(client: HiSLIPClient):
    """Test IEEE 488.2 standard commands."""
    print("\n=== Testing Standard Commands ===")

    commands = [
        ("*IDN?", "Identify instrument"),
        ("*TST?", "Run self-test"),
        ("*OPC?", "Operation complete"),
    ]

    for cmd, desc in commands:
        print(f"\n[>] {desc}: {cmd}")
        response = client.send_scpi(cmd)
        if response:
            try:
                decoded = response.decode('utf-8', errors='replace').strip()
                print(f"[<] {decoded}")
            except Exception as e:
                print(f"[<] (raw) {response.hex()}")


def test_gpio_commands(client: HiSLIPClient):
    """Test GPIO control commands."""
    print("\n=== Testing GPIO Commands ===")

    # Configure GPIO2 as output
    print("\n[>] Configure GPIO2 as output")
    response = client.send_scpi("GPIO:CONFIG 2 OUTPUT")
    if response:
        print(f"[<] {response.decode('utf-8', errors='replace').strip()}")

    time.sleep(0.2)

    # Set GPIO2 high
    print("\n[>] Set GPIO2 high")
    response = client.send_scpi("GPIO:SET 2 1")
    if response:
        print(f"[<] {response.decode('utf-8', errors='replace').strip()}")

    time.sleep(0.2)

    # Read GPIO2 state
    print("\n[>] Read GPIO2 state")
    response = client.send_scpi("GPIO:GET? 2")
    if response:
        print(f"[<] {response.decode('utf-8', errors='replace').strip()}")

    time.sleep(0.2)

    # Set GPIO2 low
    print("\n[>] Set GPIO2 low")
    response = client.send_scpi("GPIO:SET 2 0")
    if response:
        print(f"[<] {response.decode('utf-8', errors='replace').strip()}")


def test_adc_commands(client: HiSLIPClient):
    """Test ADC measurement commands."""
    print("\n=== Testing ADC Commands ===")

    print("\n[>] Read ADC channel 0")
    response = client.send_scpi("ADC:READ? 0")
    if response:
        print(f"[<] {response.decode('utf-8', errors='replace').strip()}")


def main():
    """Main test script."""
    # Configuration
    if len(sys.argv) > 1:
        hostname = sys.argv[1]
    else:
        hostname = "carbon-esp32-inst.local"

    if len(sys.argv) > 2:
        try:
            port = int(sys.argv[2])
        except ValueError:
            port = 4880
    else:
        port = 4880

    print("=" * 50)
    print("HiSLIP Socket Client Test")
    print("=" * 50)

    # Create client and connect
    client = HiSLIPClient(hostname, port)

    if not client.connect():
        print("\n[-] Failed to connect to instrument")
        sys.exit(1)

    try:
        # Run test suites
        test_standard_commands(client)
        test_gpio_commands(client)
        test_adc_commands(client)

        print("\n" + "=" * 50)
        print("Tests completed")
        print("=" * 50)

    except KeyboardInterrupt:
        print("\n[!] Test interrupted by user")
    finally:
        client.close()


if __name__ == "__main__":
    main()
