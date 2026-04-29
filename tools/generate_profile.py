#!/usr/bin/env python3
"""
Generate a Carbon instrument profile.yaml from a live device.

Connects via HiSLIP, queries SYSTEM:COMMANDS?, and renders a complete
Carbon-compatible profile.yaml without any manual authoring.

Usage:
    python tools/generate_profile.py --host carbon-esp32-inst-531c.local
    python tools/generate_profile.py --host 192.168.1.42 --output my_device.yaml
    python tools/generate_profile.py --host device.local --output - | less
"""

import argparse
import json
import socket
import struct
import sys
from typing import Optional

# ── HiSLIP transport ──────────────────────────────────────────────────────────

_HEADER = 16
_MSG_INIT = 0
_MSG_DATA_END = 7


def _recv_exactly(sock: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Connection closed mid-receive")
        buf += chunk
    return buf


def _recv_message(sock: socket.socket) -> bytes:
    header = _recv_exactly(sock, _HEADER)
    payload_len = struct.unpack(">Q", header[8:16])[0]
    payload = _recv_exactly(sock, payload_len) if payload_len else b""
    return header + payload


def hislip_connect(host: str, port: int, timeout: float = 10.0) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect((host, port))
    sock.send(struct.pack(">2sBBIQ", b"HS", _MSG_INIT, 0, 0x00010000, 0))
    resp = _recv_message(sock)
    if resp[:2] != b"HS":
        raise RuntimeError("HiSLIP Initialize failed")
    return sock


def hislip_send(sock: socket.socket, command: str) -> str:
    cmd_bytes = (command + "\n").encode()
    sock.send(struct.pack(">2sBBIQ", b"HS", _MSG_DATA_END, 0, 1, len(cmd_bytes)) + cmd_bytes)
    resp = _recv_message(sock)
    return resp[_HEADER:].decode("utf-8", errors="replace").strip()


# ── SCPI → YAML helpers ───────────────────────────────────────────────────────

# Human-readable names for IEEE 488.2 commands
_IEEE488_NAMES = {
    "*IDN?": ("identify",              "Identify instrument"),
    "*RST":  ("reset",                 "Reset to defaults"),
    "*CLS":  ("clear_status",          "Clear status registers"),
    "*TST?": ("self_test",             "Self-test"),
    "*OPC?": ("operation_complete",    "Operation complete query"),
    "*WAI":  ("wait",                  "Wait to continue"),
    "*ESR?": ("event_status_register", "Read event status register"),
    "*ESE?": ("event_status_enable_query", "Read event status enable register"),
    "*ESE":  ("event_status_enable",   "Set event status enable register"),
}


def _scpi_to_slug(scpi: str) -> str:
    """'GPIO:SET' → 'gpio_set', '*IDN?' → 'identify'."""
    if scpi in _IEEE488_NAMES:
        return _IEEE488_NAMES[scpi][0]
    slug = scpi.replace(":", "_").replace("?", "").replace(" ", "_")
    return slug.lower().strip("_")


def _param_type(json_type: str) -> str:
    # Carbon variables only support: int, float, bool, enum
    return {"int": "int", "float": "float", "bool": "bool", "enum": "enum"}.get(json_type, "int")


# Known response types for standard commands and common patterns.
# Carbon response types: int, float, bool, enum, tuple, array
_RESPONSE_TYPES: dict = {
    "*IDN?":     "enum",    # free-form string — MANUFACTURER,MODEL,SERIAL,FIRMWARE
    "*TST?":     "int",
    "*OPC?":     "bool",   # completion flag — always returns 1 when pending ops finish
    "*ESR?":     "int",
    "*ESE?":     "int",
    "UART:READ?":     "enum",   # received bytes returned as text
    "GPIO:GET?":      "bool",   # digital pin level: 0 (low) or 1 (high)
    "SYST:ERR?":      "enum",   # returns code,"message" e.g. 0,"No error"
    "SYST:ERR:COUN?": "int",    # queue depth
}

_RESPONSE_UNITS: dict = {
    "ADC:READ?": "V",
}

def _response_type(scpi: str) -> str:
    if scpi in _RESPONSE_TYPES:
        return _RESPONSE_TYPES[scpi]
    upper = scpi.upper()
    if any(kw in upper for kw in ["ADC", "VOLT", "CURR", "TEMP", "MEAS"]):
        return "float"
    if any(kw in upper for kw in ["GPIO", "DIG", "PIN", "STATUS"]):
        return "int"
    return "int"

def _response_unit(scpi: str) -> Optional[str]:
    if scpi in _RESPONSE_UNITS:
        return _RESPONSE_UNITS[scpi]
    upper = scpi.upper()
    if "VOLT" in upper:
        return "V"
    if "CURR" in upper:
        return "A"
    if "FREQ" in upper:
        return "Hz"
    if "TEMP" in upper:
        return "C"
    return None


def _default_value(param: dict) -> str:
    # Prefer the explicit default set in the descriptor
    if param.get("default"):
        return param["default"]
    t = param.get("type", "string")
    if t in ("int", "float"):
        return str(int(param["min"])) if "min" in param else "0"
    if t == "enum":
        vals = param.get("values", [])
        return vals[0] if vals else ""
    if t == "bool":
        return "0"
    return ""


def _collect_variables(commands: list) -> dict:
    """Return ordered dict of {param_name: param_dict}, deduplicated across commands.
    String-type params are excluded — Carbon variables don't support the string type."""
    seen: dict = {}
    for cmd in commands:
        for p in cmd.get("params", []):
            if p.get("type") == "string":
                continue
            name = p["name"]
            if name not in seen:
                seen[name] = p
    return seen


def _build_template(scpi: str, params: list) -> str:
    # String-type params are excluded from templates since they can't be Carbon variables
    typed = [p for p in params if p.get("type") != "string"]
    if not typed:
        return scpi
    args = " ".join(f"{{{p['name']}}}" for p in typed)
    return f"{scpi} {args}"


def _qs(val: str) -> str:
    """Quote a YAML scalar if it contains characters that need quoting."""
    if not val:
        return '""'
    if any(c in val for c in ':*?"{}[]|>&!%@`\\#'):
        escaped = val.replace("\\", "\\\\").replace('"', '\\"')
        return f'"{escaped}"'
    return val


# ── YAML renderer ─────────────────────────────────────────────────────────────

def generate_yaml(identity: dict, commands: list, hostname: str, port: int = 4880) -> str:
    manufacturer = identity.get("manufacturer", "UNKNOWN")
    model        = identity.get("model",         "UNKNOWN")
    serial       = identity.get("serial",        "")

    lines: list[str] = []

    def ln(s: str = "") -> None:
        lines.append(s)

    # ── Header ────────────────────────────────────────────────────────────────
    ln("# Carbon Instrument Profile")
    ln(f"# Generated from {hostname}  ({manufacturer} {model}  s/n {serial})")
    ln()
    ln("apiVersion: v1alpha1")
    ln()

    # ── Identity ──────────────────────────────────────────────────────────────
    ln("identity:")
    ln(f"  manufacturer: {manufacturer}")
    ln(f"  model: {model}")
    ln('  identityCommand: "*IDN?"')
    ln()

    # ── Metadata ──────────────────────────────────────────────────────────────
    ln("metadata:")
    ln(f"  description: {manufacturer} {model} instrument")
    ln()

    # ── Connection ────────────────────────────────────────────────────────────
    ln("connection:")
    ln('  terminator: "\\n"')
    ln("  bus:")
    ln("    - type: lan")
    ln(f"      defaultPort: {port}")
    ln("      timeout: 5000")
    ln()

    # ── Capabilities (manual) ─────────────────────────────────────────────────
    ln("# TODO: fill in hardware capabilities (cannot be derived from commands alone)")
    ln("# capabilities:")
    ln("#   digital:")
    ln("#     - name: GPIO")
    ln("#       pins: [2, 4, 5, 13, 14, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33]")
    ln("#   analog:")
    ln("#     - name: ADC")
    ln("#       channels: [0, 1, 2, 3, 4, 5, 6, 7]")
    ln("#       resolution: 12")
    ln("#       maxVoltage: 3.3")
    ln("#   serial:")
    ln("#     - name: UART")
    ln("#       ports: [1]")
    ln("#       baudRates: [9600, 19200, 38400, 57600, 115200]")
    ln()

    # ── Variables ─────────────────────────────────────────────────────────────
    variables = _collect_variables(commands)
    if variables:
        ln("variables:")
        for name, param in variables.items():
            ln(f"  {name}:")
            desc = param.get("description", "")
            if desc:
                ln(f"    description: {_qs(desc)}")
            ln(f"    type: {_param_type(param.get('type', 'string'))}")
            if param.get("type") == "enum" and param.get("values"):
                ln("    options:")
                for v in param["values"]:
                    ln(f"      - value: {v}")
                    ln(f"        description: {v}")
            default = _default_value(param)
            if default:
                ln(f"    default: {_qs(default)}")
        ln()

    # ── Commands ──────────────────────────────────────────────────────────────
    ln("commands:")
    has_error_query = any(cmd["scpi"] in ("SYST:ERR?", "LERR?") for cmd in commands)
    for cmd in commands:
        scpi    = cmd["scpi"]
        params  = cmd.get("params", [])
        slug    = _scpi_to_slug(scpi)
        tmpl    = _build_template(scpi, params)
        c_type  = "read" if cmd["type"] == "query" else "write"
        group   = cmd.get("group", "")
        desc    = cmd.get("description") or _IEEE488_NAMES.get(scpi, (None, scpi))[1]
        timeout = cmd.get("timeout_ms", 5000)

        ln(f"  {slug}:")
        ln(f"    description: {_qs(desc)}")
        ln(f"    type: {c_type}")
        if group:
            ln(f"    group: {group}")
        ln(f"    command: {_qs(tmpl)}")
        if c_type == "read":
            ln("    response:")
            ln(f"      type: {_response_type(scpi)}")
            unit = _response_unit(scpi)
            if unit:
                ln(f"      unit: {unit}")
        ln(f"    timeout: {timeout}")
        ln()

    if not has_error_query:
        ln("  error_query:")
        ln("    description: Read error from SCPI error queue")
        ln("    type: read")
        ln("    group: IEEE488")
        ln('    command: "SYST:ERR?"')
        ln("    response:")
        ln("      type: tuple")
        ln("      fields:")
        ln("        - name: code")
        ln("          type: int")
        ln("          description: Error code (0 = no error)")
        ln("        - name: message")
        ln("          type: enum")
        ln("          description: Error message string")
        ln("    timeout: 500")
        ln()

    # ── Discovery ─────────────────────────────────────────────────────────────
    ln("discovery:")
    ln("  mdns:")
    ln("    serviceType: _hislip._tcp")
    ln("    txtRecords:")
    ln(f"      model: {model}")
    ln(f"      vendor: {manufacturer.title()}")
    ln('      version: "1.0"')
    ln()

    return "\n".join(lines)


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate a Carbon profile.yaml from a live ESP32 instrument."
    )
    parser.add_argument("--host",   required=True, help="Device hostname or IP address")
    parser.add_argument("--port",   type=int, default=4880, help="HiSLIP port (default 4880)")
    parser.add_argument("--output", default="-",
                        help="Output file path, or - for stdout (default: -)")
    args = parser.parse_args()

    # Connect and query
    print(f"Connecting to {args.host}:{args.port} ...", file=sys.stderr)
    try:
        sock = hislip_connect(args.host, args.port)
    except (socket.timeout, ConnectionRefusedError, socket.gaierror, OSError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    print("Querying SYSTEM:COMMANDS? ...", file=sys.stderr)
    try:
        raw = hislip_send(sock, "SYSTEM:COMMANDS?")
    finally:
        sock.close()

    # Parse
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"Error: invalid JSON from device: {e}", file=sys.stderr)
        print(f"Raw ({len(raw)} bytes): {raw[:300]!r}", file=sys.stderr)
        sys.exit(1)

    identity = data.get("identity", {})
    commands = data.get("commands", [])
    print(f"Received {len(commands)} commands.", file=sys.stderr)

    # Generate
    yaml_out = generate_yaml(identity, commands, args.host, args.port)

    # Write
    if args.output == "-":
        print(yaml_out, end="")
    else:
        with open(args.output, "w") as f:
            f.write(yaml_out)
        print(f"Written to {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
