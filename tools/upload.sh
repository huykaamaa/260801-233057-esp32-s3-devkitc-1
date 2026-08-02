#!/bin/bash
# Normal firmware upload - PRESERVES NVS settings (MQTT/OSC config, distance thresholds).
# Use this for regular code updates.

set -e

PIO="${PIO:-$(command -v pio || echo ~/.platformio/penv/Scripts/platformio.exe)}"

echo "===================================="
echo "  Normal Upload (settings preserved)"
echo "===================================="
echo ""

"$PIO" run -e esp32-s3-devkitc-1 -t upload "$@"

echo ""
echo "Upload complete. Device will reboot with existing NVS config."
