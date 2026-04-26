#!/bin/bash
# mDNS Discovery Test Script

echo "=== mDNS Discovery Test ==="
echo ""

# Check if avahi-browse is available (Linux)
if command -v avahi-browse &> /dev/null; then
    echo "Using avahi-browse to scan for HiSLIP services..."
    echo "Press Ctrl+C to stop scanning"
    echo ""
    avahi-browse -r -t _hislip._tcp
    exit 0
fi

# Check if dns-sd is available (macOS)
if command -v dns-sd &> /dev/null; then
    echo "Using dns-sd to scan for HiSLIP services..."
    echo "Press Ctrl+C to stop scanning"
    echo ""
    dns-sd -B _hislip._tcp
    exit 0
fi

# Fallback: manual multicast test
echo "Neither avahi-browse nor dns-sd found."
echo ""
echo "Manual test options:"
echo "1. Install avahi-utils (Linux): sudo apt-get install avahi-utils"
echo "2. Use dns-sd (macOS): dns-sd -B _hislip._tcp"
echo "3. Check multicast connectivity:"
echo "   ping 224.0.0.251"
echo ""
echo "Also check:"
echo "- ESP32 serial output for mDNS initialization messages"
echo "- Router settings for AP isolation or mDNS filtering"
echo "- Firewall rules blocking UDP port 5353"
