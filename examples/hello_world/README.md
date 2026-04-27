# Hello World Example

The simplest possible Carbon ESP32 SDK example. Demonstrates the core developer experience:

1. **Write custom functions** — Standard C functions that do whatever you need
2. **Register them** — Map SCPI command strings to your functions
3. **Done** — The SDK handles networking, discovery, and protocol

## What This Example Does

Registers four simple commands:

- **`HELLO?`** — Returns "Hello, World!"
- **`GREET:NAME "Alice"`** — Personalized greeting with a parameter
- **`COUNTER?`** — Stateful counter that increments on each call
- **`ECHO "message"`** — Echoes back whatever you send

## Building

```bash
cd examples/hello_world
idf.py set-target esp32
idf.py menuconfig  # Set WiFi SSID/password
idf.py build
idf.py flash monitor
```

## Generate the Instrument Profile

After the device boots and connects to WiFi, generate the profile YAML by querying the live device.
Run this from the repository root:

```bash
# Use the mDNS hostname (shown in serial monitor output) or the device's IP address
python tools/generate_profile.py \
    --host carbon-esp32-inst-<mac>.local \
    --output carbon_esp32_instrument.yaml
```

The script connects via HiSLIP, queries `SYSTEM:COMMANDS?`, and writes `carbon_esp32_instrument.yaml`
with all registered commands. Re-run it any time you change your command registrations.

## Testing

Once the profile is generated:

```bash
# Discover the device — copy the VISA address from the output
carbond discover

# Use the address from discover in every exec call
ADDRESS="TCPIP0::192.168.1.100::hislip0::INSTR"

carbond exec --address "$ADDRESS" carbon_esp32_instrument.yaml hello
carbond exec --address "$ADDRESS" carbon_esp32_instrument.yaml greet_name --var name=Alice
carbond exec --address "$ADDRESS" carbon_esp32_instrument.yaml counter
carbond exec --address "$ADDRESS" carbon_esp32_instrument.yaml echo --var message="Test 123"
```

Or use Python + pyvisa:

```python
import pyvisa
rm = pyvisa.ResourceManager('@py')
inst = rm.open_resource('TCPIP0::192.168.1.100::hislip0::INSTR')

print(inst.query('HELLO?'))           # "Hello, World!"
print(inst.query('GREET:NAME "Bob"')) # "Hello, Bob! Welcome to Carbon SDK."
print(inst.query('COUNTER?'))         # "1"
print(inst.query('COUNTER?'))         # "2"
print(inst.query('ECHO "test"'))      # "test"

inst.close()
```

## The Developer Experience

Look at `main/hello_world_example.c` — the entire user-facing API is:

```c
// Step 1: Write a handler
static int cmd_hello(const char *cmd, char *response, size_t n) {
    return snprintf(response, n, "Hello, World!");
}

// Step 2: Describe and register it
static const carbon_cmd_descriptor_t hello_cmd = {
    .scpi_command = "HELLO?",
    .type         = CARBON_CMD_QUERY,
    .description  = "Returns a friendly greeting",
    .handler      = cmd_hello,
};
carbon_register_command(&hello_cmd);

// Step 3: Start
carbon_instrument_start();
```

Everything else (HiSLIP protocol, mDNS advertising, TCP sockets, session management) is handled by the SDK.

## Key Takeaways

- **No protocol knowledge required** — Users just write C functions
- **Arbitrary logic** — Functions can do anything (GPIO, I2C, SPI, calculations, etc.)
- **Standard tools** — Works with LabVIEW, MATLAB, Python pyvisa, carbond
- **3-line integration** — `init()`, `register()`, `start()`

This is the "Express.js for embedded instruments" experience.
