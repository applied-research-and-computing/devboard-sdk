# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository

GitHub: `applied-research-and-computing/esp32-sdk`

The remote is aliased as `db`, not `origin`. Use `git push db <branch>` etc.

## Branches

Work addressing a GitHub issue must be done on a branch named `fix/<description>` (bug fixes) or `feat/<description>` (new features). Push with `git push -u db <branch>` and open PRs against `main`.

## Commits and Pull Requests

Never include internal or confidential information in commit messages, PR titles, PR descriptions, or code comments — this repository is public. This covers things like internal hostnames, IP addresses, network credentials, organizational details, personal contact information, or proprietary system names that don't belong in a public SDK.

## What This Is

The Carbon ESP32 Instrument SDK turns an ESP32 into a network-connected test instrument. It implements the HiSLIP 2.0 protocol (TCP) so the device is controllable via standard VISA/SCPI tooling (e.g., `pyvisa`), and advertises itself via mDNS so clients can find it by hostname without hardcoded IPs.

## Build Commands

Requires ESP-IDF v6.0+ on the PATH.

```bash
idf.py menuconfig        # Set WiFi credentials, device identity, feature toggles
idf.py build             # Compile firmware
idf.py -p /dev/ttyUSB0 flash    # Flash to device
idf.py -p /dev/ttyUSB0 monitor  # Serial console
idf.py fullclean         # Wipe build artifacts
```

## Running Tests

Tests are integration-level Python/shell scripts — there are no native unit tests. The device must be flashed and on the network.

```bash
python test_hislip_client.py [hostname] [port]        # Basic connectivity
python test_hislip_compliance.py [host] [--port PORT] # Full HiSLIP 2.0 compliance suite
bash test_carbon_integration.sh                       # SCPI command integration tests
bash test_mdns.sh                                     # mDNS discovery
```

Python environment requires Python 3.12+ with `pyvisa` and `pyvisa-py` (see `pyproject.toml`).

## Architecture

### Startup Flow

```
main.c → WiFi init (event group sync) → carbon_instrument_start()
    ├── scpi_*_init()         register built-in command groups
    ├── mdns_service_init()   advertise hostname on LAN
    └── hislip_server_start() TCP listener loop
            └── per-client: hislip_session_t
                    ├── scpi_parse_command()      route to registry
                    ├── scpi_watchdog_dispatch()  run in isolated FreeRTOS task with timeout
                    └── carbon_param_parser       (v2 handlers only) extract typed params
```

### Command Registration

Commands are registered as static `carbon_cmd_descriptor_t` structs before `carbon_instrument_start()`. The global registry holds up to 64 entries (Kconfig-tunable).

Two handler types:
- **`handler`** — legacy: receives raw SCPI string, parses args itself
- **`handler_v2`** — modern: SDK pre-parses and validates typed parameters before the handler runs; prefer this for new commands

If `handler_v2` is set on a descriptor, the SDK uses it; otherwise falls back to `handler`.

### Response Format Convention

Use `carbon_respond_*()` helpers (`carbon_respond_float`, `carbon_respond_int`, `carbon_respond_enum`, `carbon_respond_error`) instead of raw `snprintf`. Responses must be parseable by `carbond` (the host-side daemon), so no units in numeric responses. Errors use `ERR:<code>:<message>`:
- `1` = missing required parameter
- `2` = invalid argument / out of range
- `3` = hardware failure

### HiSLIP Session State

`hislip_session_t` (in `hislip_server.c`) is per-client state: sync/async sockets, overlap mode flag, status byte, semaphores. The async channel runs as a separate FreeRTOS task per session. Overlap mode queues up to 4 commands (Kconfig-tunable) and processes them pipelined.

### Watchdog

Every command handler runs in its own FreeRTOS task. `scpi_watchdog_dispatch()` enforces the per-descriptor `timeout_ms` (default 5 s). This prevents any blocking handler from stalling the server. The watchdog task sends a task notification to itself on timeout and returns an error response.

### Configuration

All runtime configuration flows through `idf.py menuconfig` → `sdkconfig`. The component's `Kconfig` exposes: WiFi credentials, device hostname, serial number, HiSLIP port, feature flags (GPIO/ADC/UART can be compiled out), stack sizes, registry depth, overlap queue depth.

## Key Files

| File | Role |
|------|------|
| `components/carbon_instrument/include/carbon_instrument.h` | Public API: descriptor type, registration function, startup |
| `components/carbon_instrument/include/carbon_response.h` | Typed response helpers |
| `components/carbon_instrument/carbon_registry.c` | Command lookup and storage |
| `components/carbon_instrument/carbon_param_parser.c` | v2 parameter parsing (INT/FLOAT/STRING/BOOL/ENUM with range/enum validation) |
| `components/carbon_instrument/hislip_server.c` | TCP server, session lifecycle, overlap mode |
| `components/carbon_instrument/hislip.c` | HiSLIP 2.0 framing (16-byte header + payload) |
| `components/carbon_instrument/scpi_watchdog.c` | Per-command FreeRTOS task + timeout enforcement |
| `components/carbon_instrument/Kconfig` | All compile-time knobs |
| `main/main.c` | WiFi init + `carbon_instrument_start()` entry point |
| `plan-hislip-compliance-status.md` | Current HiSLIP 2.0 compliance gap analysis |
