#!/bin/bash
# Full flash erase + upload - ERASES ALL NVS SETTINGS (Preferences "distance" namespace).
# Use ONLY when: NVS looks corrupted, or a fresh board's saved config should not carry over.

set -e

PIO="${PIO:-$(command -v pio || echo ~/.platformio/penv/Scripts/platformio.exe)}"

echo "===================================="
echo "  FULL ERASE - ALL SETTINGS LOST"
echo "===================================="
echo ""
echo "This will erase:"
echo "  - MQTT server/port/user/pass/topic + full/missing payload values"
echo "  - OSC ip/port/address/value config"
echo "  - Per-sensor distance min/max thresholds + enabled flags"
echo "  - confirmTime debounce setting"
echo ""
echo "Device will boot afterwards with the hardcoded defaults in src/cantim_mqtt_new.cpp."
echo ""
read -p "Press Ctrl+C to cancel, Enter to continue... "
echo ""

echo "Erasing flash..."
"$PIO" run -e esp32-s3-devkitc-1 -t erase

echo ""
echo "Uploading firmware..."
"$PIO" run -e esp32-s3-devkitc-1 -t upload "$@"

echo ""
echo "===================================="
echo "Done. Reconfigure via the Web UI (MQTT/OSC/thresholds)."
echo "===================================="
