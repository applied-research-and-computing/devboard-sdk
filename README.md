# Carbon ESP32 Instrument SDK

Turn any ESP32 into a Carbon-compatible instrument. You write handler functions for your hardware; the SDK handles the HiSLIP server, SCPI dispatch, mDNS discovery, and Carbon integration automatically.

## How It Works

1. Write a handler function for each command your device exposes
2. Register each handler with a descriptor that names the command and describes its parameters
3. Call `carbon_instrument_start()` — the SDK starts the HiSLIP server and advertises the device on the network
4. The Carbon daemon discovers the device via mDNS and makes it available for test automation

The SDK ships with built-in commands for GPIO, ADC, and UART. Custom commands sit alongside them — from Carbon's perspective they are indistinguishable.

## Hardware Requirements

- **ESP32-WROOM-32** (or any ESP32 variant)
- USB cable for programming and power
- 2.4 GHz WiFi access point

## Software Prerequisites

- **ESP-IDF v6.0** or later
- Python 3.8+

### Install ESP-IDF

```bash
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && git checkout v6.0
./install.sh esp32
source ~/.espressif/tools/activate_idf_v6.0.sh
```

## Project Structure

```
carbon_esp32_sdk/
├── components/
│   └── carbon_instrument/          # SDK component (don't edit)
│       ├── include/
│       │   └── carbon_instrument.h # Public API — types + registration
│       ├── carbon_instrument.c     # Startup orchestration
│       ├── carbon_registry.c/h     # Command registry
│       ├── hislip.c/h              # HiSLIP protocol framing
│       ├── hislip_server.c/h       # TCP accept loop + session handling
│       ├── scpi_parser.c/h         # Registry-based SCPI dispatch
│       ├── scpi_standard.c         # Built-in IEEE 488.2 commands
│       ├── scpi_gpio.c             # Built-in GPIO commands
│       ├── scpi_adc.c              # Built-in ADC commands
│       ├── scpi_uart.c             # Built-in UART commands
│       ├── mdns_service.c/h        # mDNS advertisement
│       └── CMakeLists.txt
├── main/
│   ├── main.c                      # WiFi init + carbon_instrument_start()
│   ├── Kconfig.projbuild           # menuconfig options
│   └── CMakeLists.txt
└── CMakeLists.txt
```

## Build and Flash

### 1. Configure WiFi and Device Identity

```bash
source ~/.espressif/tools/activate_idf_v6.0.sh
idf.py menuconfig
```

Navigate to **Carbon HiSLIP Instrument Configuration** and set:

| Option | Description | Default |
|--------|-------------|---------|
| `WIFI_SSID` | WiFi network name | `myssid` |
| `WIFI_PASSWORD` | WiFi password | `mypassword` |
| `DEVICE_SERIAL` | Serial number in `*IDN?` response | `SN12345` |
| `DEVICE_HOSTNAME` | mDNS hostname prefix (MAC suffix appended) | `carbon-esp32-inst` |
| `HISLIP_SYNC_PORT` | TCP port | `4880` |

### 2. Build and Flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

Port by platform: Linux `/dev/ttyUSB0`, macOS `/dev/cu.usbserial-*`, Windows `COM3`.

### 3. Monitor

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Expected boot output:
```
I (xxxx) carbon_instrument: Registering built-in commands
I (xxxx) carbon_instrument: Starting Carbon instrument services
I (xxxx) mdns_service: HiSLIP service advertised on port 4880
I (xxxx) carbon_instrument: Carbon instrument ready
I (xxxx) hislip_server: HiSLIP listening on port 4880
```

Press `Ctrl+]` to exit.

## Adding Custom Commands

Include `carbon_instrument.h`, write your handler, declare a descriptor, register it before calling `carbon_instrument_start()`.

### Minimal example (`main/my_commands.c`)

```c
#include "carbon_instrument.h"
#include <stdio.h>
#include <string.h>

// Handler receives the full normalized SCPI string and writes the response.
// Returns the byte length of the response (0 = no response).
static int pump_speed_handler(const char *cmd, char *r, size_t n)
{
    int speed;
    sscanf(cmd + 11, "%d", &speed);  // skip "PUMP:SPEED "
    set_pump_pwm(speed);             // your hardware call
    snprintf(r, n, "OK");
    return strlen(r);
}

static int temp_read_handler(const char *cmd, char *r, size_t n)
{
    float temp = read_temperature_sensor();  // your hardware call
    snprintf(r, n, "%.2f", temp);
    return strlen(r);
}

void register_my_commands(void)
{
    static const carbon_cmd_descriptor_t pump_cmd = {
        .scpi_command = "PUMP:SPEED",
        .type         = CARBON_CMD_WRITE,
        .group        = "Pump",
        .description  = "Set pump speed as a percentage",
        .params       = {
            { .name = "speed", .type = CARBON_PARAM_INT,
              .min = 0, .max = 100, .description = "Speed %" },
        },
        .param_count  = 1,
        .timeout_ms   = 500,
        .handler      = pump_speed_handler,
    };

    static const carbon_cmd_descriptor_t temp_cmd = {
        .scpi_command = "TEMP:READ?",
        .type         = CARBON_CMD_QUERY,
        .group        = "Sensors",
        .description  = "Read temperature in Celsius",
        .param_count  = 0,
        .timeout_ms   = 1000,
        .handler      = temp_read_handler,
    };

    carbon_register_command(&pump_cmd);
    carbon_register_command(&temp_cmd);
}
```

### Wire it into `main/main.c`

```c
#include "carbon_instrument.h"

extern void register_my_commands(void);

void app_main(void)
{
    // ... NVS and WiFi init (already present) ...

    register_my_commands();       // register before starting
    carbon_instrument_start();    // starts HiSLIP server
}
```

### Descriptor fields

| Field | Type | Description |
|-------|------|-------------|
| `scpi_command` | `const char *` | Uppercase SCPI mnemonic, e.g. `"PUMP:SPEED"` or `"TEMP:READ?"` |
| `type` | `carbon_cmd_type_t` | `CARBON_CMD_WRITE` (no response expected by client) or `CARBON_CMD_QUERY` (response required) |
| `group` | `const char *` | Logical grouping for YAML generation, e.g. `"Pump"` |
| `description` | `const char *` | Human-readable description |
| `params[]` | `carbon_param_t[8]` | Parameter descriptors (see below) |
| `param_count` | `int` | Number of valid entries in `params` |
| `timeout_ms` | `int` | Expected max execution time (used in YAML) |
| `handler` | `carbon_cmd_handler_t` | Your function |

### Parameter types

| `carbon_param_type_t` | SCPI value format |
|---|---|
| `CARBON_PARAM_INT` | Integer; use `min`/`max` to constrain |
| `CARBON_PARAM_FLOAT` | Floating point; use `min`/`max` to constrain |
| `CARBON_PARAM_STRING` | Arbitrary string (e.g. for `UART:WRITE`) |
| `CARBON_PARAM_BOOL` | `0` or `1` |
| `CARBON_PARAM_ENUM` | One of the strings in `enum_values[]`; set `enum_count` |

### Important: descriptor storage

Descriptors must live for the entire program lifetime. Declare them `static const` at function scope or at file scope:

```c
// Correct — static storage duration
static const carbon_cmd_descriptor_t my_cmd = { ... };
carbon_register_command(&my_cmd);

// Wrong — stack allocation, descriptor is invalid after the function returns
carbon_cmd_descriptor_t my_cmd = { ... };  // no static
carbon_register_command(&my_cmd);
```

## Built-in Commands

These are registered automatically by the SDK on every device.

### IEEE 488.2 Standard

| Command | Type | Description |
|---------|------|-------------|
| `*IDN?` | Query | Identify instrument (`CARBON,ESP32-INSTRUMENT,<serial>,v1.0.0`) |
| `*RST` | Write | Reset to defaults |
| `*CLS` | Write | Clear status registers |
| `*TST?` | Query | Self-test (returns `0`) |
| `*OPC?` | Query | Operation complete (returns `1`) |
| `*WAI` | Write | Wait to continue |
| `*ESR?` | Query | Event status register |
| `*ESE?` | Query | Event status enable register |
| `*ESE <mask>` | Write | Set event status enable register |

### GPIO

| Command | Description |
|---------|-------------|
| `GPIO:CONFIG <pin> <INPUT\|OUTPUT>` | Set pin direction |
| `GPIO:SET <pin> <0\|1>` | Set output level |
| `GPIO:GET? <pin>` | Read pin level |

Valid pins: 2, 4, 5, 13, 14, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33

### ADC

| Command | Description |
|---------|-------------|
| `ADC:READ? <channel>` | Read voltage in volts (12-bit, 0–3.3 V) |

Channel mapping: 0=GPIO36, 1=GPIO37, 2=GPIO38, 3=GPIO39, 4=GPIO32, 5=GPIO33, 6=GPIO34, 7=GPIO35

### UART (UART1, TX=GPIO17, RX=GPIO16)

| Command | Description |
|---------|-------------|
| `UART:WRITE <data>` | Write string to UART1 |
| `UART:READ?` | Read available bytes (100 ms timeout) |
| `UART:CONFIG <baud> <bits> <parity> <stop>` | Configure port (e.g. `115200 8 NONE 1`) |

## Connecting via HiSLIP

Any HiSLIP client (VISA, Python socket, Carbon CLI) can connect on port 4880.

### Discover on the network

```bash
# Linux
avahi-browse -r _hislip._tcp

# macOS
dns-sd -B _hislip._tcp
```

The hostname is `<DEVICE_HOSTNAME>-<last4ofMAC>.local`, e.g. `carbon-esp32-inst-531c.local`.

### Python test client

```python
import socket, struct

def hislip_connect(host, port=4880):
    s = socket.socket()
    s.connect((host, port))
    s.send(struct.pack(">2sBBIQ", b"HS", 0, 0, 0x00010000, 0))  # Initialize
    s.recv(1024)
    return s

def hislip_send(s, command):
    payload = command.encode() + b"\n"
    s.send(struct.pack(">2sBBIQ", b"HS", 7, 0, 1, len(payload)) + payload)
    resp = s.recv(4096)
    return resp[16:].decode().strip()  # skip 16-byte header

s = hislip_connect("carbon-esp32-inst-531c.local")
print(hislip_send(s, "*IDN?"))
print(hislip_send(s, "GPIO:SET 2 1"))
print(hislip_send(s, "ADC:READ? 0"))
s.close()
```

## Carbon Platform Integration

Once the device is running, use the Carbon CLI to connect and send commands:

```bash
carbon instrument connect carbon-esp32-inst-531c.local
carbon instrument send "*IDN?"
carbon instrument send "GPIO:SET 2 1"
```

A `profile.yaml` (auto-generation coming in a future SDK release) describes the device's commands to the Carbon daemon for auto-completion and type checking.

## Troubleshooting

**WiFi won't connect** — Check SSID/password in menuconfig. ESP32 only supports 2.4 GHz.

**mDNS not found** — Client and device must be on the same VLAN. Some corporate networks block mDNS (UDP 5353). Find the IP from the serial monitor and connect directly.

**Build fails** — Ensure the IDF environment is active: `source ~/.espressif/tools/activate_idf_v6.0.sh`. For a clean rebuild: `idf.py fullclean && idf.py build`.

**HiSLIP connection refused** — Check serial monitor for the device IP. Test TCP: `nc -zv <ip> 4880`.

## References

- [HiSLIP Specification IVI-6.1](https://www.ivifoundation.org/downloads/Protocol%20Specifications/IVI-6.1_HiSLIP-1.1-2011-02-24.pdf)
- [SCPI Standard IEEE 488.2](https://www.ivifoundation.org/docs/scpi-99.pdf)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
