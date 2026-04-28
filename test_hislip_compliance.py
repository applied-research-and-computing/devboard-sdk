#!/usr/bin/env python3
"""
HiSLIP protocol compliance test for the Carbon ESP32 SDK.

Covers all six items from GitHub issue #8:
  1. Async channel         — AsyncInitialize, max-msg-size negotiation
  2. Overlap mode          — pipelined commands on a second session
  3. Device clear          — full four-step sync/async exchange
  4. Service request       — ASYNC_SERVICE_REQUEST emitted after DATA_END
  5. Status query          — ASYNC_STATUS_QUERY / ASYNC_STATUS_RESPONSE
  6. Trigger               — MSG_TRIGGER → DATA_END ack with echoed MessageID

Also tests (per engineering review):
  • Remote/Local control   — MSG_ASYNC_REMOTE_LOCAL_CTRL / _RESP
  • Max message size       — MSG_ASYNC_MAX_MSG_SIZE / _RESP

NOTE: Both failures below indicate the device is running stale firmware
and needs to be reflashed:
  • *STB? returning "ERROR: Unknown command"  (added in step 5)
  • Device clear hanging / socket closed       (fixed in step 3)

Usage:
    python test_hislip_compliance.py [host] [--port PORT]
    python test_hislip_compliance.py 192.168.86.82
"""

import argparse
import queue
import socket
import struct
import sys
import threading
import time

# ── Message type constants ────────────────────────────────────────────────────

MSG_INITIALIZE               =  0
MSG_INITIALIZE_RESPONSE      =  1
MSG_FATAL_ERROR              =  2
MSG_ERROR                    =  3
MSG_DATA_END                 =  7
MSG_DEVICE_CLEAR_COMPLETE    =  8
MSG_DEVICE_CLEAR_ACK         =  9
MSG_ASYNC_REMOTE_LOCAL_CTRL  = 10
MSG_ASYNC_REMOTE_LOCAL_RESP  = 11
MSG_TRIGGER                  = 12
MSG_ASYNC_MAX_MSG_SIZE       = 15
MSG_ASYNC_MAX_MSG_SIZE_RESP  = 16
MSG_ASYNC_INITIALIZE         = 17
MSG_ASYNC_INITIALIZE_RESP    = 18
MSG_ASYNC_DEVICE_CLEAR       = 19
MSG_ASYNC_SERVICE_REQUEST    = 20
MSG_ASYNC_STATUS_QUERY       = 21
MSG_ASYNC_STATUS_RESPONSE    = 22
MSG_ASYNC_DEV_CLEAR_ACK      = 23

HEADER_LEN = 16
PROLOGUE   = b"HS"

# ── Low-level framing ─────────────────────────────────────────────────────────

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

# ── Test runner ───────────────────────────────────────────────────────────────

class Compliance:
    def __init__(self, host, port):
        self.host  = host
        self.port  = port
        self._pass = 0
        self._fail = 0

    # ── Assertion helper ──────────────────────────────────────────────────────

    def _check(self, label, ok, detail=""):
        tag = "PASS" if ok else "FAIL"
        suffix = f"  ({detail})" if detail and not ok else ""
        print(f"  [{tag}] {label}{suffix}")
        if ok:
            self._pass += 1
        else:
            self._fail += 1
        return ok

    # ── Network helpers ───────────────────────────────────────────────────────

    def _connect(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((self.host, self.port))
        return s

    def _async_listener(self, sock, q):
        """Background thread: forwards every async message into q."""
        try:
            while True:
                q.put(recv_msg(sock))
        except Exception:
            pass

    def _wait_for(self, q, expected_type, timeout=5.0):
        """Pop from q until expected_type is found; preserve unmatched messages."""
        deadline = time.monotonic() + timeout
        skipped  = []
        found    = None
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                msg = q.get(timeout=remaining)
                if msg[0] == expected_type:
                    found = msg
                    break
                skipped.append(msg)
            except queue.Empty:
                break
        for m in skipped:
            q.put(m)
        return found

    # ── Protocol helpers ──────────────────────────────────────────────────────

    def _initialize(self, sock, overlap=False):
        """Return (session_id, overlap_granted) or (None, False) on failure."""
        send_msg(sock, MSG_INITIALIZE, cc=1 if overlap else 0,
                 param=(0x0100 << 16), payload=b"hislip0")
        mt, cc_out, param, _ = recv_msg(sock)
        if mt != MSG_INITIALIZE_RESPONSE:
            return None, False
        return param & 0xFFFF, bool(cc_out & 1)

    def _async_initialize(self, sock, session_id):
        send_msg(sock, MSG_ASYNC_INITIALIZE, param=session_id)
        mt, *_ = recv_msg(sock)
        return mt == MSG_ASYNC_INITIALIZE_RESP

    def _scpi(self, sock, cmd, mid=1):
        send_msg(sock, MSG_DATA_END, param=mid, payload=(cmd + "\n").encode())
        mt, _, param, payload = recv_msg(sock)
        return payload.decode(errors="replace").strip() if mt == MSG_DATA_END else None

    # ── Test sections ─────────────────────────────────────────────────────────

    def _test_sync_session(self):
        """Synchronized mode: async channel, SRQ, device clear, status query, trigger."""
        print("\n── Session 1 (synchronized mode) ────────────────────")

        sync    = self._connect()
        async_s = self._connect()
        async_q = queue.Queue()

        try:
            # ── Handshake ──────────────────────────────────────────────────
            session_id, overlap = self._initialize(sync, overlap=False)
            if not self._check("Initialize accepted", session_id is not None,
                               f"session_id={session_id}"):
                return
            self._check("Synchronized mode negotiated", not overlap)

            ok = self._async_initialize(async_s, session_id)
            if not self._check("AsyncInitialize accepted", ok):
                return

            threading.Thread(target=self._async_listener,
                             args=(async_s, async_q), daemon=True).start()

            # ── Max Message Size ───────────────────────────────────────────
            print("\n  Max Message Size negotiation")
            proposed = 65536
            send_msg(async_s, MSG_ASYNC_MAX_MSG_SIZE,
                     payload=struct.pack(">Q", proposed))
            resp = self._wait_for(async_q, MSG_ASYNC_MAX_MSG_SIZE_RESP, timeout=5.0)
            self._check("ASYNC_MAX_MSG_SIZE_RESP received", resp is not None)
            if resp and len(resp[3]) >= 8:
                negotiated = struct.unpack(">Q", resp[3][:8])[0]
                self._check("Negotiated size ≤ proposed and > 0",
                            0 < negotiated <= proposed,
                            f"negotiated={negotiated}")

            # ── *IDN? ──────────────────────────────────────────────────────
            print("\n  *IDN?")
            idn = self._scpi(sync, "*IDN?")
            self._check("*IDN? returns non-empty response",
                        idn is not None and len(idn) > 0, repr(idn))
            if idn:
                self._check("*IDN? has 4 comma-separated fields",
                            len(idn.split(",")) == 4, repr(idn))

            # ── *STB? — MAV should be set after *IDN? response ─────────────
            print("\n  *STB? / IEEE 488.2 status byte")
            stb_raw = self._scpi(sync, "*STB?", mid=2)
            try:
                stb = int(stb_raw)
                self._check("*STB? returns numeric value", True)
                self._check("MAV bit (0x10) set after *IDN? response",
                            bool(stb & 0x10), f"STB=0x{stb:02x}")
            except (TypeError, ValueError):
                self._check("*STB? returns numeric value", False,
                            f"{stb_raw!r} — reflash firmware if this fails")

            # ── Service Request ────────────────────────────────────────────
            print("\n  Service Request")
            self._scpi(sync, "*IDN?", mid=3)   # triggers SRQ
            time.sleep(0.2)
            srq = self._wait_for(async_q, MSG_ASYNC_SERVICE_REQUEST, timeout=1.0)
            self._check("ASYNC_SERVICE_REQUEST emitted after DATA_END",
                        srq is not None,
                        "none in 1 s — reflash firmware if this fails")
            if srq:
                self._check("SRQ control_code carries MAV bit",
                            bool(srq[1] & 0x10), f"cc=0x{srq[1]:02x}")

            # ── Async Status Query ─────────────────────────────────────────
            print("\n  Async Status Query")
            send_msg(async_s, MSG_ASYNC_STATUS_QUERY, cc=0)
            resp = self._wait_for(async_q, MSG_ASYNC_STATUS_RESPONSE, timeout=5.0)
            self._check("ASYNC_STATUS_RESPONSE received", resp is not None)
            if resp:
                self._check("Status byte in valid range 0–255",
                            0 <= resp[1] <= 255, f"cc={resp[1]}")

            # ── Remote / Local Control ─────────────────────────────────────
            print("\n  Remote/Local Control")
            send_msg(async_s, MSG_ASYNC_REMOTE_LOCAL_CTRL, cc=1)  # go remote
            resp = self._wait_for(async_q, MSG_ASYNC_REMOTE_LOCAL_RESP, timeout=5.0)
            self._check("ASYNC_REMOTE_LOCAL_RESP for remote request",
                        resp is not None)
            if resp:
                self._check("Response echoes remote (cc=1)",
                            resp[1] == 1,
                            f"cc={resp[1]} — reflash firmware if this fails")

            send_msg(async_s, MSG_ASYNC_REMOTE_LOCAL_CTRL, cc=0)  # go local
            resp = self._wait_for(async_q, MSG_ASYNC_REMOTE_LOCAL_RESP, timeout=5.0)
            self._check("ASYNC_REMOTE_LOCAL_RESP for local request",
                        resp is not None)
            if resp:
                self._check("Response echoes local (cc=0)",
                            resp[1] == 0, f"cc={resp[1]}")

            # ── Device Clear ───────────────────────────────────────────────
            print("\n  Device Clear")
            send_msg(async_s, MSG_ASYNC_DEVICE_CLEAR)
            try:
                mt, _, _, _ = recv_msg(sync)
            except (ConnectionError, socket.timeout) as e:
                self._check("DEVICE_CLEAR_COMPLETE on sync channel", False,
                            f"{e} — reflash firmware if this fails")
                return
            self._check("DEVICE_CLEAR_COMPLETE on sync channel",
                        mt == MSG_DEVICE_CLEAR_COMPLETE, f"type={mt}")
            if mt != MSG_DEVICE_CLEAR_COMPLETE:
                return
            send_msg(sync, MSG_DEVICE_CLEAR_ACK)
            ack = self._wait_for(async_q, MSG_ASYNC_DEV_CLEAR_ACK, timeout=5.0)
            self._check("ASYNC_DEV_CLEAR_ACK on async channel",
                        ack is not None, "not received in 5 s")

            stb_after = self._scpi(sync, "*STB?", mid=4)
            try:
                self._check("*STB? is 0 after device clear",
                            int(stb_after) == 0, f"STB={stb_after}")
            except (TypeError, ValueError):
                self._check("*STB? numeric after device clear",
                            False, repr(stb_after))

            # ── Trigger ────────────────────────────────────────────────────
            # Protocol: server sends DATA_END (zero-payload) ack with echoed MessageID.
            # Whether the registered carbon_register_trigger() callback fires is an
            # application-layer concern verified at instrument integration time.
            print("\n  Trigger")
            send_msg(sync, MSG_TRIGGER, param=0xBEEF)
            mt, _, param, _ = recv_msg(sync)
            self._check("DATA_END ack received for TRIGGER",
                        mt == MSG_DATA_END, f"type={mt}")
            self._check("Trigger ack echoes MessageID",
                        param == 0xBEEF, f"param=0x{param:x}")

        finally:
            async_s.close()
            sync.close()
            time.sleep(0.3)   # let server advance session_id

    def _test_overlap_session(self):
        """Overlap mode: negotiate and pipeline four commands."""
        print("\n── Session 2 (overlap mode) ──────────────────────────")

        sync = self._connect()
        try:
            session_id, overlap = self._initialize(sync, overlap=True)
            if not self._check("Initialize accepted", session_id is not None):
                return
            if not self._check("Overlap mode granted by server", overlap,
                               "server returned synchronized — reflash firmware if this fails"):
                return

            # Send all 4 queries before reading any response
            mids = list(range(1, 5))
            for mid in mids:
                send_msg(sync, MSG_DATA_END, param=mid, payload=b"*IDN?\n")

            # Collect responses
            responses = []
            for _ in mids:
                mt, _, param, payload = recv_msg(sync)
                if mt == MSG_DATA_END:
                    responses.append((param, payload.decode(errors="replace").strip()))

            self._check("All 4 pipelined responses received",
                        len(responses) == 4, f"got {len(responses)}")
            if len(responses) == 4:
                got_ids = [r[0] for r in responses]
                self._check("Responses arrive in submission order",
                            got_ids == mids, f"order={got_ids}")
                all_idn = all(len(r[1].split(",")) == 4 for r in responses)
                self._check("All responses are valid *IDN? strings", all_idn)
        finally:
            sync.close()

    # ── Entry point ───────────────────────────────────────────────────────────

    def run(self):
        print(f"HiSLIP compliance  →  {self.host}:{self.port}\n")
        try:
            self._test_sync_session()
            self._test_overlap_session()
        except (ConnectionRefusedError, socket.timeout, OSError) as e:
            print(f"\n[!] Network error: {e}")
            self._fail += 1

        total = self._pass + self._fail
        print(f"\n{'─'*52}")
        print(f"Results: {self._pass}/{total} passed", end="")
        if self._fail:
            print(f"  ({self._fail} FAILED)")
        else:
            print("  ✓ all passed")
        return self._fail == 0


def main():
    ap = argparse.ArgumentParser(
        description="HiSLIP protocol compliance test for Carbon ESP32 SDK")
    ap.add_argument("host", nargs="?", default="192.168.86.82",
                    help="Device IP address (default: 192.168.86.82)")
    ap.add_argument("--port", type=int, default=4880,
                    help="HiSLIP port (default: 4880)")
    args = ap.parse_args()
    sys.exit(0 if Compliance(args.host, args.port).run() else 1)


if __name__ == "__main__":
    main()
