#!/bin/bash
# Build and flash firmware to the SC01 Plus.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-/dev/cu.usbserial-02D386E0}"
ENV_NAME="${CLAWDMETER_ENV:-m5stack_core2_aws}"

echo "=== Flashing Claude Usage Tracker ==="
echo "Port: $PORT"
echo "Env:  $ENV_NAME"
echo ""

cd "$SCRIPT_DIR/firmware"
PIO="${PIO:-$HOME/Library/Python/3.9/bin/pio}"
if [ ! -x "$PIO" ]; then
    PIO="$HOME/.platformio/penv/bin/pio"
fi
"$PIO" run -e "$ENV_NAME" -t upload --upload-port "$PORT"

echo ""
echo "=== Done! ==="
