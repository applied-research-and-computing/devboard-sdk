#!/usr/bin/env python3
"""
Concurrency tests for Carbon ESP32 SDK (issue #13).

Validates the always-on worker task and fast-command bypass:

  1. Fast commands (*IDN?, SYST:ERR?, etc.) respond in < 500 ms in sync mode
  2. Sync mode slow command blocks server until complete (~2 s)
  3. Overlap mode: *IDN? (fast path) returns before queued slow command
  4. Overlap mode: queue-full error when > HISLIP_WORKER_QUEUE_DEPTH commands queued
  5. Disconnect mid-command: server recovers and accepts new connections
  6. Device clear: interrupts executing handler, server remains responsive

Requires TEST:SLOW to be registered in the firmware (added to main/main.c).
TEST:SLOW sleeps 2 s and returns 1.

Usage:
    python test_concurrent.py <host> [--port PORT] [--queue-depth N] [--test 1,3,5]
"""

import argparse
import socket
import struct
import sys
import time

HEADER_LEN  = 16
PROLOGUE    = b"HS"
SLOW_DELAY  = 2.0   # must match vTaskDelay in test_slow_handler

MSG_INITIALIZE            =  0
MSG_INITIALIZE_RESPONSE   =  1
MSG_ERROR                 =  3
MSG_DATA_END              =  7
MSG_DEVICE_CLEAR_COMPLETE =  8
MSG_DEVICE_CLEAR_ACK      =  9
MSG_ASYNC_INITIALIZE      = 17
MSG_ASYNC_INITIALIZE_RESP = 18
MSG_ASYNC_DEVICE_CLEAR    = 19
MSG_ASYNC_DEV_CLEAR_ACK   = 23


# ── Low-level framing (mirrors test_hislip_compliance.py) ─────────────────────

def _recv_n(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("socket closed mid-receive")
        buf += chunk
    return buf


def send_msg(sock, msg_type, cc=0, param=0, payload=b""):
    hdr = struct.pack(">2sBBIQ", PROLOGUE, msg_type, cc, param, len(payload))
    sock.sendall(hdr + payload)


def recv_msg(sock):
    hdr  = _recv_n(sock, HEADER_LEN)
    if hdr[:2] != PROLOGUE:
        raise ValueError(f"bad prologue {hdr[:2]!r}")
    mt   = hdr[2]
    cc   = hdr[3]
    para = struct.unpack(">I", hdr[4:8])[0]
    plen = struct.unpack(">Q", hdr[8:16])[0]
    pl   = _recv_n(sock, plen) if plen else b""
    return mt, cc, para, pl


def connect(host, port, overlap=False, timeout=10.0):
    """Open sync channel; return (sock, session_id, overlap_granted)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((host, port))
    send_msg(s, MSG_INITIALIZE, cc=1 if overlap else 0,
             param=(0x0100 << 16), payload=b"hislip0")
    mt, cc_out, param, _ = recv_msg(s)
    if mt != MSG_INITIALIZE_RESPONSE:
        raise ConnectionError(f"expected INITIALIZE_RESPONSE, got msg_type={mt}")
    return s, param & 0xFFFF, bool(cc_out & 1)


def scpi(sock, cmd, mid=1):
    """Send one SCPI command (blocking); return (response_text, elapsed_s)."""
    payload = (cmd + "\n").encode()
    t0 = time.monotonic()
    send_msg(sock, MSG_DATA_END, param=mid, payload=payload)
    mt, _, _, pl = recv_msg(sock)
    elapsed = time.monotonic() - t0
    text = pl.decode(errors="replace").strip() if mt == MSG_DATA_END else f"<msg_type={mt}>"
    return text, elapsed


# ── Result tracker ─────────────────────────────────────────────────────────────

class R:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, label, ok, detail=""):
        tag = "PASS" if ok else "FAIL"
        note = f"  ({detail})" if detail else ""
        print(f"  [{tag}] {label}{note}")
        if ok:
            self.passed += 1
        else:
            self.failed += 1
        return ok

    def summary(self):
        total = self.passed + self.failed
        print(f"\n  {self.passed}/{total} checks passed")
        return self.failed == 0


# ── Test 1: Fast commands respond quickly in sync mode ─────────────────────────

def _settle(secs=0.4):
    """Brief pause so the server can finish session teardown before reconnecting."""
    time.sleep(secs)


def test_fast_commands(host, port, r):
    print("\n── Test 1: Fast commands respond immediately in sync mode ──────────")
    fast_cmds = [
        "*IDN?", "*CLS", "*OPC?", "*STB?", "*ESR?",
        "SYST:ERR?", "SYST:ERR:COUN?",
    ]
    sock, _, _ = connect(host, port, overlap=False)
    try:
        for i, cmd in enumerate(fast_cmds, start=1):
            resp, elapsed_s = scpi(sock, cmd, mid=i)
            ms = elapsed_s * 1000
            r.check(f"{cmd!r} < 500 ms (got {ms:.0f} ms)", elapsed_s < 0.5,
                    repr(resp))
    finally:
        sock.close()


# ── Test 2: Sync mode slow command blocks until complete ───────────────────────

def test_sync_slow(host, port, r):
    _settle()
    print("\n── Test 2: Sync mode slow command blocks until complete ────────────")
    sock, _, _ = connect(host, port, overlap=False, timeout=SLOW_DELAY + 5)
    try:
        resp, elapsed = scpi(sock, "TEST:SLOW", mid=1)
        r.check(f"TEST:SLOW returns a response (got {resp!r})",
                resp not in ("", "<msg_type=3>"))
        r.check(f"TEST:SLOW took ≥ {SLOW_DELAY:.1f} s (got {elapsed:.2f} s)",
                elapsed >= SLOW_DELAY - 0.3)
    finally:
        sock.close()


# ── Test 3: Overlap mode — fast command bypasses queued slow command ───────────

def test_overlap_fast_bypass(host, port, r):
    print("\n── Test 3: Overlap mode — *IDN? bypasses queued slow command ───────")
    sock, _, granted = connect(host, port, overlap=True, timeout=SLOW_DELAY + 5)
    try:
        if not r.check("Server granted overlap mode", granted):
            return

        # Send TEST:SLOW first (will queue on worker, mid=1)
        send_msg(sock, MSG_DATA_END, param=1, payload=b"TEST:SLOW\n")
        t_slow_sent = time.monotonic()

        # Small pause to let the worker dequeue and start the slow command
        time.sleep(0.15)

        # Send *IDN? (fast path inline on server task, mid=2)
        send_msg(sock, MSG_DATA_END, param=2, payload=b"*IDN?\n")
        t_fast_sent = time.monotonic()

        # Collect both responses in arrival order
        responses = {}
        for _ in range(2):
            mt, _, mid, pl = recv_msg(sock)
            responses[mid] = (pl.decode(errors="replace").strip(), time.monotonic())

        r.check("Received *IDN? response (mid=2)", 2 in responses)
        r.check("Received TEST:SLOW response (mid=1)", 1 in responses)

        if 1 in responses and 2 in responses:
            t_fast_recv = responses[2][1]
            t_slow_recv = responses[1][1]
            fast_ms = (t_fast_recv - t_fast_sent) * 1000
            r.check(f"*IDN? returned before TEST:SLOW "
                    f"(IDN={fast_ms:.0f} ms, SLOW={t_slow_recv - t_slow_sent:.2f} s)",
                    t_fast_recv < t_slow_recv)
            r.check(f"*IDN? elapsed < 500 ms (got {fast_ms:.0f} ms)", fast_ms < 500)
    finally:
        sock.close()


# ── Test 4: Overlap mode — queue-full error ────────────────────────────────────

def test_overlap_queue_full(host, port, r, queue_depth):
    print(f"\n── Test 4: Overlap mode — queue full (depth={queue_depth}) ──────────────")
    # Send queue_depth+2 slow commands. The worker holds 1; the queue holds queue_depth.
    # Total capacity = queue_depth+1, so command queue_depth+2 must be rejected.
    # The MSG_ERROR reply for the rejected command arrives after the server-side
    # xQueueSend timeout (~1 s). Once we see it we disconnect immediately — we
    # don't wait for the slow commands to drain, which would take O(minutes).
    n_send = queue_depth + 2
    # Socket timeout covers the 1-second xQueueSend wait on the server plus margin.
    sock, _, granted = connect(host, port, overlap=True, timeout=3.0)
    queue_full_received = False
    try:
        if not r.check("Server granted overlap mode", granted):
            return

        for i in range(1, n_send + 1):
            send_msg(sock, MSG_DATA_END, param=i, payload=b"TEST:SLOW\n")

        # Read responses until we see the queue-full SCPI error (-350,"Queue overflow")
        # sent as DATA_END. Worker responses also arrive as DATA_END — we stop as soon as
        # we see the -350 payload. The error arrives after the server-side xQueueSend
        # timeout (~1 s); worker responses arrive every ~2 s, so the error comes first.
        for _ in range(n_send):
            try:
                mt, _, _, pl = recv_msg(sock)
                text = pl.decode(errors="replace").strip() if pl else ""
                if mt == MSG_DATA_END and text.startswith("-350"):
                    queue_full_received = True
                    break
            except (socket.timeout, ConnectionError):
                break

        r.check("Queue-full -350 error response received", queue_full_received)
    finally:
        sock.close()

    # Allow the server to finish cleaning up the aborted session.
    time.sleep(0.5)

    # Verify the server accepts a fresh connection after the queue-full disconnect.
    try:
        s2, _, _ = connect(host, port, timeout=5.0)
        resp, _ = scpi(s2, "*IDN?")
        r.check("Server responsive after queue-full disconnect",
                resp != "" and "ERROR" not in resp, repr(resp))
        s2.close()
    except Exception as exc:
        r.check("Server responsive after queue-full disconnect", False, str(exc))


# ── Test 5: Disconnect mid-command, server recovers ───────────────────────────

def test_disconnect_mid_command(host, port, r):
    print("\n── Test 5: Disconnect mid-command, server recovers ─────────────────")
    sock, _, _ = connect(host, port, overlap=False, timeout=10.0)
    try:
        send_msg(sock, MSG_DATA_END, param=1, payload=b"TEST:SLOW\n")
        time.sleep(0.5)   # let the worker start executing
    finally:
        sock.close()
        print("    (disconnected mid-command, waiting for server cleanup)")

    time.sleep(1.5)

    try:
        sock2, _, _ = connect(host, port, overlap=False, timeout=8.0)
        resp, elapsed = scpi(sock2, "*IDN?", mid=1)
        r.check("Server accepts new connection after mid-command disconnect",
                resp != "" and "ERROR" not in resp, repr(resp))
        r.check(f"*IDN? responds in < 500 ms after reconnect (got {elapsed*1000:.0f} ms)",
                elapsed < 0.5)
        sock2.close()
    except Exception as exc:
        r.check("Server accepts new connection after mid-command disconnect",
                False, str(exc))


# ── Test 6: Device clear during slow command ──────────────────────────────────

def test_device_clear(host, port, r):
    print("\n── Test 6: Device clear during slow command ────────────────────────")
    sync_sock,  session_id, _ = connect(host, port, overlap=False, timeout=SLOW_DELAY + 5)
    async_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    async_sock.settimeout(8.0)
    async_sock.connect((host, port))
    try:
        # Open async channel on session
        send_msg(async_sock, MSG_ASYNC_INITIALIZE, param=session_id)
        mt, _, _, _ = recv_msg(async_sock)
        if not r.check("AsyncInitialize accepted", mt == MSG_ASYNC_INITIALIZE_RESP,
                       f"got msg_type={mt}"):
            return

        # Start slow command on sync channel (don't wait for response)
        send_msg(sync_sock, MSG_DATA_END, param=1, payload=b"TEST:SLOW\n")
        time.sleep(0.3)   # let worker start the handler

        # Issue device clear on async channel
        t0 = time.monotonic()
        send_msg(async_sock, MSG_ASYNC_DEVICE_CLEAR)

        # Server sends DEVICE_CLEAR_COMPLETE on sync channel
        mt, _, _, _ = recv_msg(sync_sock)
        r.check("DEVICE_CLEAR_COMPLETE received on sync channel",
                mt == MSG_DEVICE_CLEAR_COMPLETE, f"got msg_type={mt}")

        # Acknowledge on sync channel
        send_msg(sync_sock, MSG_DEVICE_CLEAR_ACK)

        # Server sends ASYNC_DEV_CLEAR_ACK on async channel
        mt_ack, _, _, _ = recv_msg(async_sock)
        elapsed = time.monotonic() - t0
        r.check(f"ASYNC_DEV_CLEAR_ACK received within 7 s (got {elapsed:.2f} s)",
                mt_ack == MSG_ASYNC_DEV_CLEAR_ACK, f"got msg_type={mt_ack}")

        # Server should still be responsive
        sync_sock.settimeout(5.0)
        resp, _ = scpi(sync_sock, "*IDN?", mid=2)
        r.check("Server responsive after device clear",
                resp != "" and "ERROR" not in resp, repr(resp))

    finally:
        sync_sock.close()
        async_sock.close()


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Concurrency tests for Carbon ESP32 SDK (issue #13)"
    )
    parser.add_argument("host", help="Device hostname or IP")
    parser.add_argument("--port", type=int, default=4880)
    parser.add_argument("--queue-depth", type=int, default=8,
                        help="HISLIP_WORKER_QUEUE_DEPTH (default: 8)")
    parser.add_argument("--test", metavar="N", action="append",
                        help="Run only test N (1-6); repeat to run multiple")
    args = parser.parse_args()

    print("=" * 60)
    print(" Carbon ESP32 Concurrent Command Execution Tests (#13)")
    print("=" * 60)
    print(f" Device:      {args.host}:{args.port}")
    print(f" Slow delay:  {SLOW_DELAY:.1f} s  (TEST:SLOW handler)")
    print(f" Queue depth: {args.queue_depth}  (HISLIP_WORKER_QUEUE_DEPTH)")

    r   = R()
    run = set(args.test or ["1", "2", "3", "4", "5", "6"])

    try:
        if "1" in run: test_fast_commands(args.host, args.port, r)
        if "2" in run: test_sync_slow(args.host, args.port, r)
        if "3" in run: test_overlap_fast_bypass(args.host, args.port, r)
        if "4" in run: test_overlap_queue_full(args.host, args.port, r, args.queue_depth)
        if "5" in run: test_disconnect_mid_command(args.host, args.port, r)
        if "6" in run: test_device_clear(args.host, args.port, r)
    except KeyboardInterrupt:
        print("\n[!] Interrupted")

    print("\n" + "=" * 60)
    ok = r.summary()
    print("=" * 60)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
