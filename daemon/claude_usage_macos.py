#!/usr/bin/env python3
"""macOS sender for Clawdmeter.

The original project ships a BlueZ/systemd shell daemon for Linux. This variant
uses USB serial by default on macOS because background Bluetooth is often gated
by TCC permissions. BLE remains available as a fallback/override. It can read
either Claude Code OAuth headers or the local oh-my-claudecode usage cache when
no token file is available.
"""

from __future__ import annotations

import asyncio
import glob
import json
import os
import getpass
import re
import subprocess
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib import request

DEVICE_NAME = os.environ.get("CLAWDMETER_DEVICE_NAME", "Claude Controller")
RX_CHAR_UUID = "4c41555a-4465-7669-6365-000000000002"
REQ_CHAR_UUID = "4c41555a-4465-7669-6365-000000000004"
POLL_INTERVAL = int(os.environ.get("CLAWDMETER_POLL_INTERVAL", "60"))
SERIAL_PORT = os.environ.get("CLAWDMETER_SERIAL_PORT")
TRANSPORT = os.environ.get("CLAWDMETER_TRANSPORT", "auto").lower()
SERIAL_BAUD = int(os.environ.get("CLAWDMETER_SERIAL_BAUD", "115200"))
HTTP_HOST = os.environ.get("CLAWDMETER_HTTP_HOST", "0.0.0.0")
HTTP_PORT = int(os.environ.get("CLAWDMETER_HTTP_PORT", "8787"))

HOME = Path.home()
CREDENTIALS = HOME / ".claude" / ".credentials.json"
USAGE_CACHE = HOME / ".claude" / "plugins" / "oh-my-claudecode" / ".usage-cache-anthropic.json"
KEYCHAIN_SERVICE = "Claude Code-credentials"


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def reset_minutes(value: str | None) -> int:
    if not value:
        return 0
    try:
        target = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return 0
    return max(0, round((target - datetime.now(timezone.utc)).total_seconds() / 60))


def read_cache_payload() -> dict[str, Any] | None:
    if not USAGE_CACHE.exists():
        return None
    raw = json.loads(USAGE_CACHE.read_text())
    if raw.get("error"):
        return None
    data = raw.get("data") or {}
    return {
        "s": int(data.get("fiveHourPercent") or 0),
        "sr": reset_minutes(data.get("fiveHourResetsAt")),
        "w": int(data.get("weeklyPercent") or 0),
        "wr": reset_minutes(data.get("weeklyResetsAt")),
        "st": "cached",
        "ok": True,
    }


def extract_access_token(blob: str) -> str | None:
    blob = blob.strip()
    if not blob:
        return None
    try:
        data = json.loads(blob)
    except json.JSONDecodeError:
        data = None
    if isinstance(data, dict):
        if isinstance(data.get("accessToken"), str):
            return data["accessToken"]
        for value in data.values():
            if isinstance(value, dict) and isinstance(value.get("accessToken"), str):
                return value["accessToken"]
    match = re.search(r'"accessToken"\s*:\s*"([^"]+)"', blob)
    if match:
        return match.group(1)
    if re.fullmatch(r"[A-Za-z0-9_\-.~+/=]{20,}", blob):
        return blob
    return None


def read_access_token() -> str | None:
    try:
        out = subprocess.run(
            [
                "security",
                "find-generic-password",
                "-s",
                KEYCHAIN_SERVICE,
                "-a",
                getpass.getuser(),
                "-w",
            ],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        )
        token = extract_access_token(out.stdout)
        if token:
            return token
    except Exception as exc:
        log(f"Keychain token read failed: {exc}")

    if not CREDENTIALS.exists():
        return None
    try:
        return extract_access_token(CREDENTIALS.read_text())
    except OSError:
        return None


def poll_anthropic_payload() -> dict[str, Any] | None:
    token = read_access_token()
    if not token:
        return None

    body = json.dumps({
        "model": "claude-haiku-4-5-20251001",
        "max_tokens": 1,
        "messages": [{"role": "user", "content": "hi"}],
    }).encode()
    req = request.Request(
        "https://api.anthropic.com/v1/messages",
        data=body,
        headers={
            "Authorization": f"Bearer {token}",
            "anthropic-version": "2023-06-01",
            "anthropic-beta": "oauth-2025-04-20",
            "Content-Type": "application/json",
            "User-Agent": "claude-code/2.1.5",
        },
        method="POST",
    )
    try:
        with request.urlopen(req, timeout=20) as resp:
            headers = resp.headers
    except Exception as exc:
        log(f"Anthropic poll failed: {exc}")
        return None

    now = time.time()
    s_reset = float(headers.get("anthropic-ratelimit-unified-5h-reset") or now)
    w_reset = float(headers.get("anthropic-ratelimit-unified-7d-reset") or now)
    return {
        "s": round(float(headers.get("anthropic-ratelimit-unified-5h-utilization") or 0) * 100),
        "sr": max(0, round((s_reset - now) / 60)),
        "w": round(float(headers.get("anthropic-ratelimit-unified-7d-utilization") or 0) * 100),
        "wr": max(0, round((w_reset - now) / 60)),
        "st": headers.get("anthropic-ratelimit-unified-5h-status") or "allowed",
        "ok": True,
    }


def usage_payload() -> dict[str, Any]:
    payload = poll_anthropic_payload() or read_cache_payload()
    if not payload:
        payload = {"s": 0, "sr": 0, "w": 0, "wr": 0, "st": "no-data", "ok": False}
    now = datetime.now()
    payload.update({
        "y": now.year,
        "m": now.month,
        "d": now.day,
        "hh": now.hour,
        "mm": now.minute,
        "ss": now.second,
    })
    return payload


class UsageHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path not in {"/", "/usage"}:
            self.send_error(404)
            return
        body = json.dumps(usage_payload(), separators=(",", ":")).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args: Any) -> None:
        log("HTTP " + (fmt % args))


def serve_http_loop() -> None:
    server = ThreadingHTTPServer((HTTP_HOST, HTTP_PORT), UsageHandler)
    log(f"Serving usage at http://{HTTP_HOST}:{HTTP_PORT}/usage")
    server.serve_forever()


def find_serial_port() -> str | None:
    if SERIAL_PORT:
        return SERIAL_PORT
    candidates: list[str] = []
    for pattern in (
        "/dev/cu.usbserial-*",
        "/dev/cu.SLAB_USBtoUART",
        "/dev/cu.wchusbserial*",
    ):
        candidates.extend(sorted(glob.glob(pattern)))
    return candidates[0] if candidates else None


def send_serial_loop(port: str) -> None:
    import serial

    while True:
        try:
            log(f"Opening serial port {port} at {SERIAL_BAUD}...")
            with serial.Serial(port, SERIAL_BAUD, timeout=0.2, write_timeout=5) as ser:
                time.sleep(1.0)
                while True:
                    payload = json.dumps(usage_payload(), separators=(",", ":"))
                    line = (payload + "\n").encode()
                    log(f"Serial sending: {payload}")
                    ser.write(line)
                    ser.flush()

                    deadline = time.time() + 2
                    while time.time() < deadline:
                        raw = ser.readline()
                        if not raw:
                            continue
                        text = raw.decode("utf-8", "replace").strip()
                        if text:
                            log(f"Device: {text}")
                            if text in {"SERIAL_ACK", "SERIAL_NACK"}:
                                break

                    time.sleep(POLL_INTERVAL)
        except Exception as exc:
            log(f"Serial loop failed: {exc}")
            time.sleep(5)


async def find_device():
    from bleak import BleakScanner

    log(f"Scanning for {DEVICE_NAME!r}...")
    devices = await BleakScanner.discover(timeout=8)
    for dev in devices:
        if dev.name == DEVICE_NAME:
            log(f"Found {dev.name} ({dev.address})")
            return dev
    return None


async def send_loop() -> None:
    from bleak import BleakClient

    while True:
        dev = await find_device()
        if dev is None:
            log("Device not found; retrying")
            await asyncio.sleep(5)
            continue

        try:
            async with BleakClient(dev) as client:
                log("Connected")

                def refresh_handler(_: int, data: bytearray) -> None:
                    log(f"Refresh requested by device: {data.hex()}")

                try:
                    await client.start_notify(REQ_CHAR_UUID, refresh_handler)
                except Exception as exc:
                    log(f"Refresh notifications unavailable: {exc}")

                while client.is_connected:
                    payload = json.dumps(usage_payload(), separators=(",", ":"))
                    log(f"Sending: {payload}")
                    await client.write_gatt_char(RX_CHAR_UUID, payload.encode(), response=False)
                    await asyncio.sleep(POLL_INTERVAL)
        except Exception as exc:
            log(f"BLE loop failed: {exc}")
            await asyncio.sleep(5)


if __name__ == "__main__":
    if TRANSPORT == "http":
        serve_http_loop()
    elif TRANSPORT != "ble":
        port = find_serial_port()
        if port:
            send_serial_loop(port)
        elif TRANSPORT == "serial":
            while True:
                log("No serial port found; retrying")
                time.sleep(5)
                port = find_serial_port()
                if port:
                    send_serial_loop(port)
        else:
            log("No serial port found; falling back to BLE")
            asyncio.run(send_loop())
    else:
        asyncio.run(send_loop())
