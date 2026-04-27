# Carbon ESP32 Instrument SDK - Beginner's Guide

Welcome! This guide will help you understand and use the Carbon ESP32 Instrument SDK, even if you're new to embedded development or SDK design.

---

## Table of Contents

1. [What This SDK Does](#what-this-sdk-does)
2. [Key Concepts](#key-concepts)
3. [How It Works](#how-it-works)
4. [Getting Started](#getting-started)
5. [Adding Your Own Commands](#adding-your-own-commands)
6. [Built-In Features](#built-in-features)
7. [File Structure](#file-structure)
8. [Common Tasks](#common-tasks)
9. [Troubleshooting](#troubleshooting)
10. [Next Steps](#next-steps)

---

## What This SDK Does

### The Big Picture

This SDK turns a cheap ESP32 microcontroller (the little WiFi chip you can buy for ~$5) into a **network-connected test instrument** that can be controlled remotely over WiFi. Think of it like making a DIY oscilloscope, multimeter, or custom lab equipment that you can talk to from your computer.

### What Problem Does It Solve?

When you're testing hardware or running automated tests, you often need to:
- Toggle GPIO pins on/off
- Read sensor values (like voltage from an ADC)
- Send/receive data over UART
- Control custom hardware (pumps, motors, LEDs, etc.)

Normally, you'd have to write all the networking code, protocol handling, and command parsing yourself. **This SDK does all that boring stuff for you.**

### Real-World Example

Imagine you're building a temperature-controlled fan:
- **Without this SDK**: Write WiFi code, TCP server, command parser, error handling, network discovery... then finally write the fan control logic.
- **With this SDK**: Write 20 lines of code for "read temperature" and "set fan speed", register them as commands, done.

---

## Key Concepts

### 1. ESP32 & ESP-IDF

- **ESP32**: A low-cost microcontroller with built-in WiFi and Bluetooth
- **ESP-IDF**: Espressif's official development framework (like Arduino, but more powerful and professional)
- **FreeRTOS**: A real-time operating system that runs on the ESP32, letting you run multiple tasks concurrently

### 2. HiSLIP Protocol

**HiSLIP** (High-Speed LAN Instrument Protocol) is an industry-standard protocol for controlling test instruments over TCP/IP. It's used by oscilloscopes, signal generators, and other lab equipment.

- Runs over TCP (port 4880 by default)
- Handles message framing, error recovery, and device locking
- Designed for reliable instrument control

### 3. SCPI Commands

**SCPI** (Standard Commands for Programmable Instruments) is a text-based command language for instruments.

Examples:
```
*IDN?              → Identify the device (returns manufacturer, model, serial, version)
*RST               → Reset the device
GPIO:SET 2 1       → Set GPIO pin 2 to HIGH
ADC:READ? 0        → Read voltage on ADC channel 0
UART:SEND "Hello"  → Send "Hello" over UART
```

Commands ending in `?` are **queries** (they return data). Commands without `?` are **actions** (they do something).

### 4. mDNS (Network Discovery)

**mDNS** (Multicast DNS) lets devices advertise themselves on the local network without needing a DNS server.

Your ESP32 shows up as:
```
carbon-esp32-inst-<last4-of-mac>.local
```

So you can connect to it by name instead of hunting for its IP address.

---

## How It Works

### The Flow (Step-by-Step)

1. **You write handler functions** for whatever your hardware does:
   ```c
   int my_led_handler(const char *cmd, char *response, size_t max) {
       gpio_set_level(GPIO_NUM_2, 1);  // Turn on LED on pin 2
       return 0;  // No response needed
   }
   ```

2. **You register each command** with a descriptor:
   ```c
   carbon_cmd_descriptor_t led_cmd = {
       .scpi_command = "LED:ON",
       .handler = my_led_handler,
       .help_text = "Turn on the LED",
       .category = "Custom"
   };
   carbon_register_command(&led_cmd);
   ```

3. **You call `carbon_instrument_start()`** and the SDK:
   - Connects to WiFi
   - Starts a HiSLIP server on port 4880
   - Advertises itself via mDNS
   - Listens for commands and routes them to your handlers

4. **Clients send commands** over the network:
   ```python
   # From Python
   import socket
   sock = socket.socket()
   sock.connect(('carbon-esp32-inst-531c.local', 4880))
   sock.send(b'LED:ON\n')
   ```

### Architecture Diagram

```
┌─────────────────────────────────────────────────────┐
│  Client (Python, Carbon CLI, LabVIEW, etc.)        │
└─────────────────┬───────────────────────────────────┘
                  │ HiSLIP over TCP
                  ▼
┌─────────────────────────────────────────────────────┐
│  ESP32 (Your Device)                                │
│  ┌───────────────────────────────────────────────┐  │
│  │  HiSLIP Server (hislip_server.c)              │  │
│  │  - Accepts connections                        │  │
│  │  - Handles protocol framing                   │  │
│  └──────────────┬────────────────────────────────┘  │
│                 │                                    │
│  ┌──────────────▼────────────────────────────────┐  │
│  │  SCPI Parser (scpi_parser.c)                  │  │
│  │  - Parses commands like "GPIO:SET 2 1"        │  │
│  │  - Routes to registered handlers              │  │
│  └──────────────┬────────────────────────────────┘  │
│                 │                                    │
│  ┌──────────────▼────────────────────────────────┐  │
│  │  Command Handlers                             │  │
│  │  - Built-in: GPIO, ADC, UART                  │  │
│  │  - Your custom handlers                       │  │
│  └──────────────┬────────────────────────────────┘  │
│                 │                                    │
│  ┌──────────────▼────────────────────────────────┐  │
│  │  Hardware (GPIO pins, ADC, UART, etc.)        │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

---

## Getting Started

### Prerequisites

1. **Hardware**: ESP32 development board (ESP32-DevKitC, ESP32-WROOM, etc.)
2. **Software**:
   - ESP-IDF v4.0 or later ([installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
   - USB cable to connect ESP32 to your computer
   - Serial terminal (built into ESP-IDF)

### Build and Flash

1. **Clone this repository**:
   ```bash
   git clone <repo-url>
   cd esp32_sdk
   ```

2. **Configure WiFi credentials**:
   ```bash
   idf.py menuconfig
   ```
   Navigate to: `Component config → Carbon Instrument → WiFi Configuration`
   - Set SSID (your WiFi network name)
   - Set Password

3. **Build the project**:
   ```bash
   idf.py build
   ```

4. **Flash to ESP32**:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
   (Replace `/dev/ttyUSB0` with your serial port: `COM3` on Windows, `/dev/cu.usbserial-*` on macOS)

5. **Watch the serial output**:
   You should see:
   ```
   I (1234) wifi: connected to MyWiFi
   I (2345) carbon_instrument: HiSLIP server started on port 4880
   I (2346) mdns: mDNS service started: carbon-esp32-inst-531c.local
   ```

### Test It

From another computer on the same network:

```bash
# Using netcat
echo "*IDN?" | nc carbon-esp32-inst-531c.local 4880

# Using Python
python3
>>> import socket
>>> s = socket.socket()
>>> s.connect(('carbon-esp32-inst-531c.local', 4880))
>>> s.send(b'*IDN?\n')
>>> s.recv(1024)
b'CARBON,ESP32-INSTRUMENT,SN12345,v1.0.0\n'
```

---

## Adding Your Own Commands

### Example: Temperature Sensor

Let's add a command to read a temperature sensor connected to GPIO 4.

#### Step 1: Write the Handler Function

In `main/main.c`:

```c
#include "driver/gpio.h"
#include "driver/adc.h"

// Pretend we have a temperature sensor on ADC1 channel 0 (GPIO 36)
int temp_read_handler(const char *cmd, char *response, size_t max_len) {
    // Read ADC value
    int raw = adc1_get_raw(ADC1_CHANNEL_0);
    
    // Convert to temperature (example formula, adjust for your sensor)
    float voltage = (raw / 4095.0) * 3.3;  // ESP32 ADC is 12-bit, 0-3.3V
    float temp_c = (voltage - 0.5) * 100;  // Example: TMP36 sensor formula
    
    // Format response
    snprintf(response, max_len, "%.2f", temp_c);
    return 0;  // Success
}
```

#### Step 2: Register the Command

Still in `main/main.c`, inside `app_main()`:

```c
void app_main(void) {
    // ... existing WiFi setup code ...
    
    // Configure ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    
    // Register temperature command
    carbon_cmd_descriptor_t temp_cmd = {
        .scpi_command = "TEMP:READ?",
        .handler = temp_read_handler,
        .help_text = "Read temperature in Celsius",
        .category = "Sensors",
        .min_params = 0,
        .max_params = 0
    };
    carbon_register_command(&temp_cmd);
    
    // Start the instrument
    carbon_instrument_start();
}
```

#### Step 3: Rebuild and Test

```bash
idf.py build flash monitor
```

Then from your client:
```bash
echo "TEMP:READ?" | nc carbon-esp32-inst-531c.local 4880
# Response: 23.45
```

### Handler Function Signature

All command handlers must follow this signature:

```c
int handler_function(const char *cmd, char *response, size_t max_len);
```

- **`cmd`**: The full command string received (e.g., `"GPIO:SET 2 1"`)
- **`response`**: Buffer to write the response into (for query commands ending in `?`)
- **`max_len`**: Maximum size of the response buffer
- **Return value**: `0` on success, `-1` on error

### Parsing Parameters

Use `sscanf()` to extract parameters from the command:

```c
int gpio_set_handler(const char *cmd, char *response, size_t max_len) {
    int pin, value;
    if (sscanf(cmd, "GPIO:SET %d %d", &pin, &value) != 2) {
        return -1;  // Invalid parameters
    }
    
    gpio_set_level(pin, value);
    return 0;
}
```

---

## Built-In Features

The SDK comes with ready-to-use commands for common tasks.

### Standard SCPI Commands

| Command | Description | Example Response |
|---------|-------------|------------------|
| `*IDN?` | Identify device | `CARBON,ESP32-INSTRUMENT,SN12345,v1.0.0` |
| `*RST` | Reset device | (no response) |
| `*TST?` | Self-test | `0` (pass) or `1` (fail) |
| `SYST:ERR?` | Get last error | `0,"No error"` |

### GPIO Commands

| Command | Description | Example |
|---------|-------------|---------|
| `GPIO:CONF <pin> <mode>` | Configure pin (0=input, 1=output) | `GPIO:CONF 2 1` |
| `GPIO:SET <pin> <value>` | Set output level (0=low, 1=high) | `GPIO:SET 2 1` |
| `GPIO:GET? <pin>` | Read input level | `GPIO:GET? 4` → `1` |

### ADC Commands

| Command | Description | Example |
|---------|-------------|---------|
| `ADC:READ? <channel>` | Read voltage (0-3.3V) | `ADC:READ? 0` → `1.65` |

Channels map to GPIO pins:
- Channel 0 = GPIO 36
- Channel 1 = GPIO 37
- Channel 2 = GPIO 38
- Channel 3 = GPIO 39
- (See ESP32 datasheet for full mapping)

### UART Commands

| Command | Description | Example |
|---------|-------------|---------|
| `UART:SEND "<data>"` | Send string over UART | `UART:SEND "Hello"` |
| `UART:READ?` | Read available data | `UART:READ?` → `"Response"` |

---

## File Structure

```
esp32_sdk/
├── main/
│   └── main.c                    # Your application entry point
│                                 # - WiFi setup
│                                 # - Register custom commands
│                                 # - Call carbon_instrument_start()
│
├── components/
│   └── carbon_instrument/        # The SDK itself (don't edit unless contributing)
│       ├── include/
│       │   └── carbon_instrument.h   # Public API you use
│       ├── carbon_instrument.c       # Core initialization
│       ├── hislip_server.c           # TCP server, HiSLIP protocol
│       ├── scpi_parser.c             # Command parsing and routing
│       ├── scpi_gpio.c               # Built-in GPIO commands
│       ├── scpi_adc.c                # Built-in ADC commands
│       ├── scpi_uart.c               # Built-in UART commands
│       └── mdns_service.c            # Network discovery
│
├── CMakeLists.txt                # Build configuration
├── sdkconfig                     # ESP-IDF configuration (generated)
├── SEB.MD                        # Project metadata
└── README.md                     # Quick reference
```

### What You'll Edit

- **`main/main.c`**: Add your custom command handlers and register them
- **`sdkconfig`**: Configure WiFi, serial port, etc. (via `idf.py menuconfig`)

### What You Won't Edit (Usually)

- **`components/carbon_instrument/`**: The SDK core (unless you're fixing bugs or adding features)

---

## Common Tasks

### Change WiFi Credentials

```bash
idf.py menuconfig
# Navigate to: Component config → Carbon Instrument → WiFi Configuration
# Update SSID and Password
idf.py build flash
```

### Change HiSLIP Port

In `main/main.c`:

```c
carbon_config_t config = {
    .port = 5000,  // Change from default 4880
    // ... other config ...
};
carbon_instrument_start_with_config(&config);
```

### Add Multiple Commands at Once

```c
carbon_cmd_descriptor_t my_commands[] = {
    {
        .scpi_command = "LED:ON",
        .handler = led_on_handler,
        .help_text = "Turn on LED",
        .category = "LED"
    },
    {
        .scpi_command = "LED:OFF",
        .handler = led_off_handler,
        .help_text = "Turn off LED",
        .category = "LED"
    },
    {
        .scpi_command = "LED:BLINK",
        .handler = led_blink_handler,
        .help_text = "Blink LED",
        .category = "LED"
    }
};

for (int i = 0; i < 3; i++) {
    carbon_register_command(&my_commands[i]);
}
```

### Return Errors from Handlers

```c
int my_handler(const char *cmd, char *response, size_t max_len) {
    int value;
    if (sscanf(cmd, "MYCOMMAND %d", &value) != 1) {
        snprintf(response, max_len, "ERROR: Invalid parameter");
        return -1;  // Error
    }
    
    if (value < 0 || value > 100) {
        snprintf(response, max_len, "ERROR: Value out of range (0-100)");
        return -1;
    }
    
    // Do something with value...
    return 0;  // Success
}
```

### Debug Serial Output

The SDK uses ESP-IDF's logging system. You'll see output like:

```
I (1234) wifi: connected to MyWiFi
I (2345) carbon_instrument: HiSLIP server started
E (3456) scpi_parser: Unknown command: INVALID:CMD
```

- `I` = Info
- `W` = Warning
- `E` = Error
- `D` = Debug (disabled by default)

To enable debug logs:
```bash
idf.py menuconfig
# Component config → Log output → Default log verbosity → Debug
```

---

## Troubleshooting

### ESP32 Won't Connect to WiFi

**Symptoms**: Serial output shows `wifi: failed to connect` or timeout errors.

**Solutions**:
1. Double-check SSID and password in `menuconfig`
2. Make sure your WiFi is 2.4 GHz (ESP32 doesn't support 5 GHz)
3. Check if your router has MAC filtering enabled
4. Try moving the ESP32 closer to the router

### Can't Find Device on Network

**Symptoms**: `nc: carbon-esp32-inst-*.local: Name or service not known`

**Solutions**:
1. Check that mDNS is working:
   ```bash
   # Linux/macOS
   avahi-browse -a | grep carbon
   
   # macOS
   dns-sd -B _hislip._tcp
   ```
2. Try using the IP address directly (check serial output for `IP: 192.168.1.123`)
3. Some corporate networks block mDNS — try a home network
4. On Windows, install [Bonjour Print Services](https://support.apple.com/kb/DL999)

### Commands Not Working

**Symptoms**: Send a command, get no response or error.

**Solutions**:
1. Check serial output for error messages
2. Make sure command ends with `\n` (newline)
3. Verify command syntax matches registered `scpi_command` exactly (case-sensitive)
4. Check handler function is returning `0` on success

### Build Errors

**Symptoms**: `idf.py build` fails with errors.

**Solutions**:
1. Make sure ESP-IDF is properly installed: `idf.py --version`
2. Clean and rebuild: `idf.py fullclean && idf.py build`
3. Check for typos in `CMakeLists.txt`
4. Make sure all `#include` paths are correct

### Device Keeps Rebooting

**Symptoms**: Serial output shows boot messages repeating.

**Solutions**:
1. Check for stack overflow (increase stack size in `xTaskCreate` calls)
2. Look for null pointer dereferences in your handler code
3. Check power supply — ESP32 needs stable 3.3V with enough current (~500mA)
4. Disable watchdog temporarily to debug: `menuconfig → Component config → ESP32-specific → Task watchdog`

---

## Next Steps

### Learn More About ESP-IDF

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)

### Explore Advanced Features

- **Add SPI/I2C support**: Control sensors and displays
- **Implement data logging**: Store measurements to SD card or flash
- **Add authentication**: Require password for commands
- **Stream data**: Send continuous measurements (e.g., oscilloscope mode)
- **Multi-client support**: Handle multiple simultaneous connections

### Contribute to the SDK

Found a bug? Want to add a feature? Contributions are welcome!

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-new-feature`
3. Make your changes
4. Test thoroughly
5. Submit a pull request

---

## Glossary

- **ADC**: Analog-to-Digital Converter (reads voltages)
- **ESP-IDF**: Espressif IoT Development Framework
- **FreeRTOS**: Free Real-Time Operating System
- **GPIO**: General Purpose Input/Output (digital pins)
- **HiSLIP**: High-Speed LAN Instrument Protocol
- **mDNS**: Multicast DNS (network discovery)
- **SCPI**: Standard Commands for Programmable Instruments
- **SDK**: Software Development Kit
- **UART**: Universal Asynchronous Receiver/Transmitter (serial communication)

---

## Getting Help

- **Issues**: Open an issue on GitHub
- **Discussions**: Join the community forum
- **Documentation**: Check the README and code comments

---

**Happy building! 🚀**
