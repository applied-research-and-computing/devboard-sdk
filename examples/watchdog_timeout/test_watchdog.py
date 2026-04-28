#!/usr/bin/env python3
"""
Watchdog timeout integration test for the watchdog_timeout example.

Verifies three things:
  1. Normal commands (PING?) complete within their timeout — unaffected
  2. Hung commands (SLOW:HANG) are killed and return a SCPI error at ~2 s
  3. The device is immediately responsive after a watchdog timeout

Usage:
    uv run examples/watchdog_timeout/test_watchdog.py --host carbon-esp32-inst-XXXX.local
    uv run examples/watchdog_timeout/test_watchdog.py --host 192.168.1.42
    uv run examples/watchdog_timeout/test_watchdog.py --host 192.168.1.42 --port 4880
"""

import argparse
import time
import sys
import pyvisa


def query(inst, cmd):
    t0 = time.time()
    resp = inst.query(cmd)
    return resp.strip(), time.time() - t0


def main():
    parser = argparse.ArgumentParser(
        description="Watchdog timeout integration test."
    )
    parser.add_argument("--host", required=True,
                        help="Device hostname or IP address")
    parser.add_argument("--port", type=int, default=4880,
                        help="HiSLIP port (default: 4880)")
    args = parser.parse_args()

    visa_addr = f"TCPIP::{args.host}::hislip0,{args.port}::INSTR"
    print(f"Connecting to {visa_addr} ...")

    rm   = pyvisa.ResourceManager('@py')
    inst = rm.open_resource(visa_addr)
    inst.timeout = 10000  # 10 s pyvisa timeout — longer than the 2 s watchdog

    passed = 0
    failed = 0

    def check(label, cond, detail=""):
        nonlocal passed, failed
        if cond:
            print(f"  PASS  {label}")
            passed += 1
        else:
            print(f"  FAIL  {label}" + (f": {detail}" if detail else ""))
            failed += 1

    print("\n1) PING? — should return immediately")
    resp, dt = query(inst, "PING?")
    print(f"   [{dt:.2f}s] {resp}")
    check("response is PONG",   resp == "PONG",     f"got {resp!r}")
    check("returned in < 1 s",  dt < 1.0,           f"took {dt:.2f}s")

    print("\n2) SLOW:HANG — watchdog should fire at ~2 s")
    resp, dt = query(inst, "SLOW:HANG")
    print(f"   [{dt:.2f}s] {resp}")
    check("returned SCPI error", "-365" in resp,     f"got {resp!r}")
    check("fired in 1.8–3.0 s",  1.8 < dt < 3.0,   f"took {dt:.2f}s")

    print("\n3) PING? after timeout — device must not be hung")
    resp, dt = query(inst, "PING?")
    print(f"   [{dt:.2f}s] {resp}")
    check("response is PONG",   resp == "PONG",     f"got {resp!r}")
    check("returned in < 1 s",  dt < 1.0,           f"took {dt:.2f}s")

    inst.close()

    print(f"\n{passed} passed, {failed} failed.")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
