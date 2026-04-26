#!/bin/bash
# Test Carbon integration with ESP32 HiSLIP instrument
# Tests mDNS discovery and HiSLIP protocol connectivity

HOSTNAME="${1:-carbon-esp32-inst.local}"
CARBON_CLI="${2:-/home/hugo/Documents/ARNC/carbon/monorepo/packages/carbon-cli/carbon-linux}"

echo "=========================================="
echo "Carbon ESP32 Integration Test"
echo "=========================================="
echo "Target: $HOSTNAME"
echo "Carbon CLI: $CARBON_CLI"
echo ""

# Test 1: Validate the Carbon instrument profile
echo "[1] Validating Carbon instrument profile..."
PROFILE="$(dirname "$0")/carbon_esp32_instrument.yaml"
if [ -f "$PROFILE" ]; then
    if [ -x "$CARBON_CLI" ]; then
        echo "  > $CARBON_CLI validate --file $PROFILE"
        "$CARBON_CLI" validate --file "$PROFILE" 2>&1 && echo "[+] Profile validation passed" || echo "[!] Profile validation had issues"
    else
        echo "  [!] Carbon CLI not executable, checking YAML syntax..."
        if command -v yq &> /dev/null; then
            yq eval '.' "$PROFILE" > /dev/null && echo "[+] YAML syntax valid" || echo "[-] YAML syntax error"
        fi
    fi
else
    echo "[-] Profile file not found: $PROFILE"
fi
echo ""

# Test 2: Test mDNS discovery
echo "[2] Testing mDNS discovery..."
if command -v avahi-browse &> /dev/null; then
    echo "  Searching for _hislip._tcp services..."
    FOUND=$(avahi-browse -r _hislip._tcp 2>/dev/null | grep -i esp32 | head -1)
    if [ -n "$FOUND" ]; then
        echo "[+] Device found:"
        echo "    $FOUND"
    else
        echo "[-] No ESP32 HiSLIP device found in mDNS"
    fi
elif command -v dns-sd &> /dev/null; then
    echo "  Using dns-sd to search _hislip._tcp..."
    timeout 3 dns-sd -B _hislip._tcp 2>/dev/null | grep -i esp32 && echo "[+] Device found" || echo "[-] No device found"
else
    echo "[-] avahi-browse or dns-sd not found"
    echo "    Install: sudo apt-get install avahi-tools"
fi
echo ""

# Test 3: Test TCP connectivity
echo "[3] Testing TCP connectivity to $HOSTNAME:4880..."
if command -v nc &> /dev/null; then
    if nc -z -w 2 "$HOSTNAME" 4880 2>/dev/null; then
        echo "[+] TCP port 4880 is open"
    else
        echo "[-] Cannot reach $HOSTNAME:4880"
        echo "    Trying IP address lookup..."
        if command -v nslookup &> /dev/null; then
            IP=$(nslookup "$HOSTNAME" 2>/dev/null | grep -A 1 "Name:" | tail -1 | awk '{print $2}')
            if [ -n "$IP" ]; then
                echo "    IP: $IP"
                nc -z -w 2 "$IP" 4880 2>/dev/null && echo "[+] Connected to IP" || echo "[-] IP also unreachable"
            fi
        fi
    fi
else
    echo "[-] netcat not found, skipping TCP test"
fi
echo ""

# Test 4: Test with Python HiSLIP client
echo "[4] Testing with Python HiSLIP client..."
PYTEST="$(dirname "$0")/test_hislip_client.py"
if [ -f "$PYTEST" ]; then
    echo "  Running: python3 $PYTEST $HOSTNAME"
    python3 "$PYTEST" "$HOSTNAME" 2>&1 | head -30
    echo ""
else
    echo "[-] Python test script not found: $PYTEST"
fi
echo ""

echo "=========================================="
echo "Test Summary"
echo "=========================================="
echo "Profile: $([ -f "$PROFILE" ] && echo "✓ Present" || echo "✗ Missing")"
echo "mDNS Service: $(command -v avahi-browse &>/dev/null && echo "✓ Available" || echo "✗ Not available")"
echo "TCP Connectivity: Use test above"
echo "HiSLIP Protocol: Use Python client test above"
echo ""
echo "For full Carbon Daemon integration, follow:"
echo "  cd $(dirname "$0")/../monorepo/packages/control-daemon"
echo "  make start"
echo "=========================================="
