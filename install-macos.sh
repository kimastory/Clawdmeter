#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV="$SCRIPT_DIR/.venv"
PLIST="$HOME/Library/LaunchAgents/com.kimastory.clawdmeter.plist"

python3 -m venv "$VENV"
"$VENV/bin/python" -m pip install --upgrade pip >/dev/null
"$VENV/bin/python" -m pip install bleak pyserial >/dev/null

mkdir -p "$(dirname "$PLIST")"
cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.kimastory.clawdmeter</string>
  <key>ProgramArguments</key>
  <array>
    <string>$VENV/bin/python</string>
    <string>$SCRIPT_DIR/daemon/claude_usage_macos.py</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>CLAWDMETER_TRANSPORT</key>
    <string>serial</string>
    <key>CLAWDMETER_SERIAL_PORT</key>
    <string>/dev/cu.usbserial-02D386E0</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardOutPath</key>
  <string>$SCRIPT_DIR/daemon/macos-daemon.log</string>
  <key>StandardErrorPath</key>
  <string>$SCRIPT_DIR/daemon/macos-daemon.err.log</string>
</dict>
</plist>
PLIST

launchctl unload "$PLIST" >/dev/null 2>&1 || true
launchctl load "$PLIST"

echo "Installed and started com.kimastory.clawdmeter"
echo "Logs: $SCRIPT_DIR/daemon/macos-daemon.log"
