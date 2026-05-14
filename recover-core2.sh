#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-/dev/cu.usbserial-02D386E0}"
PIO="${PIO:-$HOME/Library/Python/3.9/bin/pio}"

if [ ! -x "$PIO" ]; then
  echo "PlatformIO not found at $PIO" >&2
  exit 1
fi

cat <<MSG
=== M5Stack Core2 recovery upload ===
Port: $PORT

Do this on the device while this script is trying to connect:
  1. Unplug any stacked/base module if possible; use the Core2 USB-C port.
  2. Hold the left power button for 6+ seconds.
  3. Single-click the left power button.
  4. Press the bottom RST button once when "Connecting..." appears.

The script retries until upload succeeds. Press Ctrl-C to stop.
MSG

while true; do
  (cd "$SCRIPT_DIR/firmware" && "$PIO" run -e m5stack_core2_aws -t upload --upload-port "$PORT") && exit 0
  echo
  echo "Upload did not connect. Retrying in 3 seconds..."
  sleep 3
done
