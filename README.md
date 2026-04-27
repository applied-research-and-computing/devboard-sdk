# ESP32 HiSLIP Instrument Server

A HiSLIP (High-Speed LAN Instrument Protocol) server implementation for ESP32-32, providing remote control of GPIO, ADC, and UART peripherals via SCPI commands over TCP/IP.

## Features

- **HiSLIP Protocol**: IEEE-1488 compliant instrument protocol over TCP/IP
  - Single TCP listener on port 4880
  - HiSLIP message type dispatch for synchronous data and asynchronous control messages
- **SCPI Command Interface**: Standard IEEE 488.2 commands plus custom subsystems
  - Standard: `*IDN?`, `*RST`, `*CLS`, `*TST?`, `*OPC?`, `*WAI`, `*ESR?`, `*ESE`
  - GPIO: Configure pins, set/get digital I/O
  - ADC: Read analog voltages
  - UART: Serial passthrough for external devices
- **mDNS Discovery**: Auto-discovery via `_hislip._tcp.local` service advertisement
- **WiFi Connectivity**: Station mode with automatic reconnection

## Hardware Requirements

- **ESP32-WROOM-32** development board
- USB cable for programming and power
- WiFi access point (2.4 GHz)

## Software Prerequisites

- **ESP-IDF v6.0** or later
- Python 3.8+
- Git

### Install ESP-IDF

```bash
# Clone ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v6.0

# Install dependencies
./install.sh esp32

# Activate environment (run this in every new terminal session)
source ~/.espressif/tools/activate_idf_v6.0.sh
```

## Build and Flash

### 1. Configure WiFi Credentials

```bash
idf.py menuconfig
```

Navigate to: **Carbon HiSLIP Instrument Configuration**

Set:
- **WiFi SSID**: Your network name
- **WiFi Password**: Your network password
- **Device Serial Number**: Unique identifier (e.g., `SN12345`)
- **Device Hostname Prefix**: mDNS hostname (default: `carbon-esp32-inst`)

Save and exit (press `S`, then `Q`).

### 2. Build Firmware

```bash
source ~/.espressif/tools/activate_idf_v6.0.sh
idf.py build
```

### 3. Flash to ESP32

Connect ESP32 via USB, then:

```bash
idf.py -p /dev/ttyUSB0 flash
```

Replace `/dev/ttyUSB0` with your serial port:
- **Linux**: `/dev/ttyUSB0` or `/dev/ttyACM0`
- **macOS**: `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART`
- **Windows**: `COM3`, `COM4`, etc.

### 4. Monitor Serial Output

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Press `Ctrl+]` to exit monitor.

## Usage

### Discover Instrument via mDNS

```bash
# Linux/macOS
avahi-browse -r _hislip._tcp

# macOS (alternative)
dns-sd -B _hislip._tcp
```

Look for hostname like `carbon-esp32-inst-aabbcc.local` (MAC address appended).

### Connect via HiSLIP

Use any HiSLIP-compatible client or raw TCP socket on **port 4880**.

#### Example: Python Socket Client

```python
import socket
import struct

# Connect to the HiSLIP listener
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('carbon-esp32-inst-aabbcc.local', 4880))

# Send Initialize message (HiSLIP handshake)
# Message format: [Prologue: 2 bytes 'HS'] [Type: 1 byte] [Control: 1 byte] 
#                 [MessageParameter: 4 bytes] [PayloadLength: 8 bytes] [Payload]
init_msg = struct.pack(">2sBBIQ", b"HS", 0, 0, 0x00010000, 0)
sock.send(init_msg)
response = sock.recv(1024)
print("Initialize response:", response.hex())

# Send SCPI command via DataEnd message (type 7)
scpi_cmd = b'*IDN?\n'
data_msg = struct.pack(">2sBBIQ", b"HS", 7, 0, 0, len(scpi_cmd)) + scpi_cmd
sock.send(data_msg)
response = sock.recv(1024)
print("SCPI response:", response)

sock.close()
```

### SCPI Command Examples

| Command | Description | Example Response |
|---------|-------------|------------------|
| `*IDN?` | Identify instrument | `CARBON,ESP32-INSTRUMENT,SN12345,v1.0.0` |
| `*RST` | Reset to defaults | `OK` |
| `*TST?` | Run self-test | `0` |
| `GPIO:CONFIG 2 OUTPUT` | Set GPIO2 as output | `OK` |
| `GPIO:SET 2 1` | Set GPIO2 high | `OK` |
| `GPIO:GET? 2` | Read GPIO2 state | `1` |
| `ADC:READ? 0` | Read ADC channel 0 | `1.234` (volts) |
| `UART:WRITE Hello` | Write to UART1 | `OK` |
| `UART:READ?` | Read available bytes from UART1 | `<data>` |
| `UART:CONFIG 115200 8 NONE 1` | Configure UART1 | `OK` |

## Project Structure

```
devboard_sdk/
├── main/
│   ├── main.c                  # Application entry point
│   ├── hislip.h/c              # HiSLIP message framing
│   ├── hislip_server.h/c       # HiSLIP server (accept loop, channel routing)
│   ├── scpi_parser.h/c         # SCPI command parser
│   ├── scpi_standard.c         # IEEE 488.2 standard commands
│   ├── scpi_gpio.c             # GPIO control commands
│   ├── scpi_adc.c              # ADC measurement commands
│   ├── scpi_uart.c             # UART passthrough commands
│   ├── mdns_service.h/c        # mDNS service advertisement
│   ├── CMakeLists.txt          # Component build config
│   └── Kconfig.projbuild       # Menuconfig options
├── CMakeLists.txt              # Top-level build config
├── sdkconfig                   # ESP-IDF configuration (generated)
├── carbon_esp32_instrument.yaml # Carbon platform instrument profile
├── plan-esp32-hislip-instrument.md # Implementation plan
├── SEB.MD                      # Project metadata
└── README.md                   # This file
```

## Configuration Options

All options accessible via `idf.py menuconfig` → **Carbon HiSLIP Instrument Configuration**:

- `WIFI_SSID`: WiFi network name
- `WIFI_PASSWORD`: WiFi password
- `DEVICE_SERIAL`: Serial number for `*IDN?` response
- `DEVICE_HOSTNAME`: mDNS hostname prefix
- `HISLIP_SYNC_PORT`: HiSLIP TCP listener port (default: 4880)

## Troubleshooting

### WiFi Connection Fails

- Check SSID/password in menuconfig
- Ensure 2.4 GHz WiFi (ESP32 doesn't support 5 GHz)
- Monitor serial output: `idf.py monitor`

### mDNS Discovery Not Working

- Ensure client and ESP32 are on same network/VLAN
- Some corporate networks block mDNS (port 5353 UDP)
- Try direct IP connection instead (check serial monitor for assigned IP)

### Build Errors

- Ensure ESP-IDF environment is activated: `source ~/.espressif/tools/activate_idf_v6.0.sh`
- Clean build: `idf.py fullclean && idf.py build`
- Check ESP-IDF version: `idf.py --version` (should be v6.0+)

### HiSLIP Connection Refused

- Verify ESP32 has IP address (check serial monitor)
- Test TCP connectivity: `nc -zv <ip> 4880`
- Check firewall rules on client machine

## Carbon Platform Integration

This instrument is designed for use with the **Carbon** test automation platform. The included `carbon_esp32_instrument.yaml` profile enables:

- Auto-discovery via mDNS
- Command auto-completion in Carbon CLI
- Type-safe command generation
- Integration with Carbon test scripts

Place the YAML profile in your Carbon instruments directory and use:

```bash
carbon instrument connect carbon-esp32-inst-aabbcc.local
carbon instrument send "*IDN?"
```

## License

MIT License - see project root for details.

## Contributing

Contributions welcome! Please submit issues and pull requests to the project repository.

## References

- [HiSLIP Specification (IVI-6.1)](https://www.ivifoundation.org/downloads/Protocol%20Specifications/IVI-6.1_HiSLIP-1.1-2011-02-24.pdf)
- [SCPI Standard (IEEE 488.2)](https://www.ivifoundation.org/docs/scpi-99.pdf)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [ESP32-WROOM-32 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf)

# Build sample application on the side
- import sdk
- timer + stopwatch
- visa commands to stopwatch, start stop lap
- firmware booted on side, expose method for controlling pins