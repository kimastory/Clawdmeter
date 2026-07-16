#!/bin/bash
# Capture a screenshot from the M5Stack Core2 info-panel over USB serial.
# Reads the resolution from the device header, so it works at any size.
# Usage: ./screenshot-core2.sh [output.png] [port]

OUTPUT="${1:-screenshot.png}"
PORT="${2:-/dev/cu.usbserial-02D386E0}"

TMPRAW=$(mktemp /tmp/screenshot_XXXXXX.raw)
DIMS=$(mktemp /tmp/screenshot_XXXXXX.dims)
trap "rm -f '$TMPRAW' '$DIMS'" EXIT

echo "Taking screenshot from $PORT..."

python3 - "$PORT" "$TMPRAW" "$DIMS" << 'PYEOF'
import serial, sys
port_path, raw_path, dims_path = sys.argv[1], sys.argv[2], sys.argv[3]

# Configure DTR/RTS low BEFORE opening so opening the CP2104 port does not
# pulse the ESP32 auto-reset line (keeps the current screen on-screen).
port = serial.Serial()
port.port = port_path
port.baudrate = 115200
port.timeout = 10
port.dtr = False
port.rts = False
port.open()
port.reset_input_buffer()
port.write(b"screenshot\n")
port.flush()

w = h = raw_size = 0
while True:
    line = port.readline().decode("utf-8", errors="replace").strip()
    if line.startswith("SCREENSHOT_START"):
        parts = line.split()
        w, h, raw_size = int(parts[1]), int(parts[2]), int(parts[3])
        break
    if line == "SCREENSHOT_ERR":
        print("Device reported screenshot error", file=sys.stderr)
        sys.exit(1)

data = b""
while len(data) < raw_size:
    chunk = port.read(min(4096, raw_size - len(data)))
    if not chunk:
        print(f"Timeout: got {len(data)} of {raw_size} bytes", file=sys.stderr)
        sys.exit(1)
    data += chunk

with open(raw_path, "wb") as f:
    f.write(data)
with open(dims_path, "w") as f:
    f.write(f"{w} {h}")

for _ in range(10):
    if port.readline().decode("utf-8", errors="replace").strip() == "SCREENSHOT_END":
        break
port.close()
print(f"Captured {w}x{h} ({len(data)} bytes)")
PYEOF

[ $? -ne 0 ] && { echo "Screenshot capture failed"; exit 1; }

read W H < "$DIMS"
ffmpeg -y -f rawvideo -pixel_format rgb565le -video_size "${W}x${H}" \
    -i "$TMPRAW" -update 1 -frames:v 1 "$OUTPUT" 2>/dev/null || true

if [ -f "$OUTPUT" ]; then
    echo "Saved: $OUTPUT (${W}x${H})"
else
    echo "Error: conversion failed"; exit 1
fi
