#!/usr/bin/env python3
"""
HiSLIP test client for the Carbon ESP32 instrument SDK.
Usage: python test_hislip_client.py [hostname] [port]
"""

import socket
import struct
import sys
import time
import json
from typing import Optional

HISLIP_HEADER_SIZE = 16  # bytes
MSG_INITIALIZE = 0
MSG_DATA_END   = 7


class HiSLIPClient:
    def __init__(self, host: str, port: int = 4880, timeout: float = 5.0):
        self.host    = host
        self.port    = port
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None

    def connect(self) -> bool:
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(self.timeout)
            print(f"[*] Connecting to {self.host}:{self.port} ...")
            self.sock.connect((self.host, self.port))

            # HiSLIP Initialize
            self.sock.send(struct.pack(">2sBBIQ", b"HS", MSG_INITIALIZE, 0, 0x00010000, 0))
            resp = self._recv_full()
            if resp[:2] != b"HS":
                print("[-] Bad Initialize response")
                return False

            print(f"[+] Connected (session {struct.unpack('>I', resp[4:8])[0] & 0xFFFF})")
            return True

        except (socket.timeout, ConnectionRefusedError, socket.gaierror, OSError) as e:
            print(f"[-] {e}")
            return False

    def _recv_full(self) -> bytes:
        """Receive one complete HiSLIP message (header + payload)."""
        header = self._recv_exactly(HISLIP_HEADER_SIZE)
        payload_len = struct.unpack(">Q", header[8:16])[0]
        payload = self._recv_exactly(payload_len) if payload_len else b""
        return header + payload

    def _recv_exactly(self, n: int) -> bytes:
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Connection closed mid-receive")
            buf += chunk
        return buf

    def send(self, command: str) -> str:
        """Send SCPI command, return decoded response string."""
        cmd_bytes = (command + "\n").encode()
        self.sock.send(
            struct.pack(">2sBBIQ", b"HS", MSG_DATA_END, 0, 1, len(cmd_bytes)) + cmd_bytes
        )
        resp = self._recv_full()
        return resp[HISLIP_HEADER_SIZE:].decode("utf-8", errors="replace").strip()

    def close(self):
        if self.sock:
            self.sock.close()


# ── Test suites ───────────────────────────────────────────────────────────────

def check(label: str, response: str, expected: str = None):
    ok = (expected is None) or response.startswith(expected)
    status = "PASS" if ok else "FAIL"
    print(f"  [{status}] {label}: {response!r}")
    return ok


def test_standard(c: HiSLIPClient):
    print("\n── Standard IEEE 488.2 ──")
    idn = c.send("*IDN?")
    parts = idn.split(",")
    ok = len(parts) == 4
    print(f"  [{'PASS' if ok else 'FAIL'}] *IDN?  → {idn!r}")
    if ok:
        print(f"         manufacturer={parts[0]}  model={parts[1]}  serial={parts[2]}  fw={parts[3]}")
    check("*TST?", c.send("*TST?"),  "0")
    check("*OPC?", c.send("*OPC?"),  "1")
    check("*RST",  c.send("*RST"),   "OK")
    check("*CLS",  c.send("*CLS"),   "OK")


def test_gpio(c: HiSLIPClient):
    print("\n── GPIO ──")
    check("CONFIG OUTPUT", c.send("GPIO:CONFIG 2 OUTPUT"), "OK")
    check("SET high",      c.send("GPIO:SET 2 1"),         "OK")
    time.sleep(0.1)
    level = c.send("GPIO:GET? 2")
    print(f"  [{'PASS' if level == '1' else 'FAIL'}] GET? after SET 1 → {level!r}")
    check("SET low",       c.send("GPIO:SET 2 0"),         "OK")
    time.sleep(0.1)
    level = c.send("GPIO:GET? 2")
    print(f"  [{'PASS' if level == '0' else 'FAIL'}] GET? after SET 0 → {level!r}")


def test_adc(c: HiSLIPClient):
    print("\n── ADC ──")
    voltage = c.send("ADC:READ? 0")
    try:
        v = float(voltage)
        print(f"  [PASS] ADC:READ? 0 → {v:.3f} V")
    except ValueError:
        print(f"  [FAIL] ADC:READ? 0 → {voltage!r}  (expected float)")


def test_introspection(c: HiSLIPClient):
    print("\n── SYSTEM:COMMANDS? (Phase 3) ──")
    raw = c.send("SYSTEM:COMMANDS?")

    try:
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"  [FAIL] Invalid JSON: {e}")
        print(f"         Raw ({len(raw)} bytes): {raw[:200]!r}...")
        return

    identity = data.get("identity", {})
    commands = data.get("commands", [])

    print(f"  [PASS] Valid JSON — {len(raw)} bytes")
    print(f"         identity: {identity.get('manufacturer')},{identity.get('model')},"
          f"{identity.get('serial')},{identity.get('firmware')}")
    print(f"         commands: {len(commands)} registered")

    # Print command list grouped
    groups: dict = {}
    for cmd in commands:
        g = cmd.get("group", "Other")
        groups.setdefault(g, []).append(cmd["scpi"])

    for group, scpis in sorted(groups.items()):
        print(f"           {group}: {', '.join(sorted(scpis))}")

    # Spot-check a few expected commands exist
    scpi_set = {c["scpi"] for c in commands}
    for expected in ["*IDN?", "GPIO:SET", "ADC:READ?", "UART:READ?"]:
        ok = expected in scpi_set
        print(f"  [{'PASS' if ok else 'FAIL'}] {expected!r} present in registry")


def test_errors(c: HiSLIPClient):
    print("\n── Error handling ──")
    check("Unknown command", c.send("NOTACOMMAND"), "ERROR")
    check("Command too long", c.send("A" * 300),    "ERROR")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "carbon-esp32-inst.local"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 4880

    print("=" * 55)
    print(" Carbon ESP32 HiSLIP Test Client")
    print("=" * 55)

    c = HiSLIPClient(host, port)
    if not c.connect():
        sys.exit(1)

    try:
        test_standard(c)
        test_gpio(c)
        test_adc(c)
        test_introspection(c)
        test_errors(c)
        print("\n" + "=" * 55)
        print(" Done")
        print("=" * 55)
    except KeyboardInterrupt:
        print("\n[!] Interrupted")
    finally:
        c.close()


if __name__ == "__main__":
    main()
