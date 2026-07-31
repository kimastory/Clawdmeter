#!/usr/bin/env python3
"""Claude Usage Tracker Daemon (BLE) — macOS port of claude-usage-daemon.sh.

Polls Claude API rate-limit headers and writes a JSON payload to the
ESP32 "Claude Controller" peripheral over a custom GATT service. Uses
bleak (CoreBluetooth backend on macOS).
"""

import asyncio
import getpass
import json
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import httpx
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

DEVICE_NAME = "Clawdmeter Monitor"
SERVICE_UUID = "4c41555a-4465-7669-6365-000000000101"
RX_CHAR_UUID = "4c41555a-4465-7669-6365-000000000102"
REQ_CHAR_UUID = "4c41555a-4465-7669-6365-000000000104"

POLL_INTERVAL = 60
TICK = 5
SCAN_TIMEOUT = 8.0
MAX_RETRY_BACKOFF = 10
MAX_SCAN_MISSES_BEFORE_RESTART = 30

# macOS: token lives in Keychain (service "Claude Code-credentials").
# Linux: token lives in ~/.claude/.credentials.json.
KEYCHAIN_SERVICE = "Claude Code-credentials"
CREDENTIALS_PATH = Path.home() / ".claude" / ".credentials.json"
SAVED_ADDR_FILE = Path.home() / ".config" / "claude-usage-monitor" / "ble-address"

USAGE_API_URL = "https://api.anthropic.com/api/oauth/usage"
TOKEN_REFRESH_URL = "https://platform.claude.com/v1/oauth/token"
OAUTH_CLIENT_ID = os.environ.get(
    "CLAUDE_CODE_OAUTH_CLIENT_ID",
    "9d1c250a-e61b-44d9-88ed-5944d1962f5e",
)
API_HEADERS_TEMPLATE = {"User-Agent": "claude-code/2.1.5"}


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def _extract_credentials(blob: str) -> dict | None:
    """Pull Claude Code OAuth credentials out of a credentials blob.

    Claude Code stores credentials as a JSON object; the blob may also be
    nested ({"claudeAiOauth": {"accessToken": "...", ...}}).
    """
    blob = blob.strip()
    if not blob:
        return None
    try:
        data = json.loads(blob)
    except json.JSONDecodeError:
        data = None
    if isinstance(data, dict):
        creds = data.get("claudeAiOauth") if isinstance(data.get("claudeAiOauth"), dict) else data
        if isinstance(creds.get("accessToken"), str):
            return {
                "accessToken": creds.get("accessToken"),
                "refreshToken": creds.get("refreshToken"),
                "expiresAt": creds.get("expiresAt"),
                "raw": data,
            }
    # Raw token (no JSON wrapper) — must look plausible (sk-ant-... etc.)
    if re.fullmatch(r"[A-Za-z0-9_\-.~+/=]{20,}", blob):
        return {"accessToken": blob}
    return None


def _read_credentials_keychain() -> dict | None:
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
    except subprocess.CalledProcessError as e:
        log(f"Keychain read failed (rc={e.returncode}): {e.stderr.strip()}")
        return None
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        log(f"Keychain access error: {e}")
        return None
    return _extract_credentials(out.stdout)


def _read_credentials_file() -> dict | None:
    try:
        raw = CREDENTIALS_PATH.read_text()
    except OSError as e:
        log(f"Error reading credentials: {e}")
        return None
    return _extract_credentials(raw)


def read_credentials() -> dict | None:
    if sys.platform == "darwin":
        return _read_credentials_keychain() or _read_credentials_file()
    return _read_credentials_file()


def _is_expired(creds: dict) -> bool:
    expires_at = creds.get("expiresAt")
    return isinstance(expires_at, (int, float)) and expires_at <= time.time() * 1000


def _write_credentials_keychain(creds: dict) -> None:
    raw = creds.get("raw")
    if not isinstance(raw, dict):
        return
    target = raw.get("claudeAiOauth") if isinstance(raw.get("claudeAiOauth"), dict) else raw
    target["accessToken"] = creds["accessToken"]
    if creds.get("refreshToken"):
        target["refreshToken"] = creds["refreshToken"]
    if creds.get("expiresAt"):
        target["expiresAt"] = creds["expiresAt"]
    try:
        subprocess.run(
            [
                "security",
                "add-generic-password",
                "-U",
                "-s",
                KEYCHAIN_SERVICE,
                "-a",
                getpass.getuser(),
                "-w",
                json.dumps(raw, separators=(",", ":")),
            ],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        log(f"Keychain credential update failed: {e}")


async def refresh_credentials(creds: dict) -> dict | None:
    refresh_token = creds.get("refreshToken")
    if not refresh_token:
        return None
    data = {
        "grant_type": "refresh_token",
        "refresh_token": refresh_token,
        "client_id": OAUTH_CLIENT_ID,
    }
    try:
        async with httpx.AsyncClient(timeout=20.0) as http:
            resp = await http.post(TOKEN_REFRESH_URL, data=data)
    except httpx.HTTPError as e:
        log(f"Token refresh failed: {e}")
        return None
    if resp.status_code != 200:
        log(f"Token refresh failed: HTTP {resp.status_code}")
        return None
    try:
        body = resp.json()
    except ValueError:
        log("Token refresh failed: invalid JSON response")
        return None
    access_token = body.get("access_token")
    if not access_token:
        log("Token refresh failed: missing access_token")
        return None
    refreshed = dict(creds)
    refreshed["accessToken"] = access_token
    refreshed["refreshToken"] = body.get("refresh_token") or refresh_token
    if body.get("expires_in"):
        refreshed["expiresAt"] = int(time.time() * 1000) + int(body["expires_in"]) * 1000
    elif body.get("expires_at"):
        refreshed["expiresAt"] = body["expires_at"]
    if sys.platform == "darwin":
        _write_credentials_keychain(refreshed)
    return refreshed


def load_cached_address() -> str | None:
    if not SAVED_ADDR_FILE.exists():
        return None
    addr = SAVED_ADDR_FILE.read_text().strip()
    # Accept both Linux MAC (AA:BB:CC:DD:EE:FF) and macOS CoreBluetooth UUID
    # (E621E1F8-C36C-495A-93FC-0C247A3E6E5F).
    if re.fullmatch(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", addr) or re.fullmatch(
        r"[0-9A-Fa-f]{8}-(?:[0-9A-Fa-f]{4}-){3}[0-9A-Fa-f]{12}", addr
    ):
        return addr
    log("Cached address malformed, discarding")
    SAVED_ADDR_FILE.unlink(missing_ok=True)
    return None


def save_address(addr: str) -> None:
    SAVED_ADDR_FILE.parent.mkdir(parents=True, exist_ok=True)
    SAVED_ADDR_FILE.write_text(addr)


async def scan_for_device() -> str | None:
    log(f"Scanning for '{DEVICE_NAME}' ({SCAN_TIMEOUT}s)...")
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT)
    for d in devices:
        if d.name == DEVICE_NAME:
            log(f"Found: {d.address}")
            return d.address
    return None


def _reset_minutes(value: str | None) -> int:
    if not value:
        return 0
    try:
        target = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return 0
    return max(0, round((target - datetime.now(timezone.utc)).total_seconds() / 60))


async def poll_api(creds: dict) -> dict | None:
    if _is_expired(creds):
        refreshed = await refresh_credentials(creds)
        if not refreshed:
            log("OAuth credentials expired and refresh failed")
            return None
        creds = refreshed

    headers = dict(API_HEADERS_TEMPLATE)
    headers["Authorization"] = f"Bearer {creds['accessToken']}"
    try:
        async with httpx.AsyncClient(timeout=20.0) as http:
            resp = await http.get(USAGE_API_URL, headers=headers)
    except httpx.HTTPError as e:
        log(f"API call failed: {e}")
        return None

    if resp.status_code == 401 and creds.get("refreshToken"):
        refreshed = await refresh_credentials(creds)
        if refreshed:
            return await poll_api(refreshed)
    if resp.status_code != 200:
        log(f"API call failed: HTTP {resp.status_code}")
        return None
    try:
        body = resp.json()
    except ValueError:
        log("API call failed: invalid JSON response")
        return None

    def pct(util) -> int:
        try:
            return int(round(float(util)))
        except (TypeError, ValueError):
            return 0

    five_hour = body.get("five_hour") or {}
    seven_day = body.get("seven_day") or {}
    if "utilization" not in five_hour and "utilization" not in seven_day:
        log("API call failed: usage fields missing")
        return None

    payload = {
        "s": pct(five_hour.get("utilization")),
        "sr": _reset_minutes(five_hour.get("resets_at")),
        "w": pct(seven_day.get("utilization")),
        "wr": _reset_minutes(seven_day.get("resets_at")),
        "st": "allowed",
        "ok": True,
    }
    now_local = datetime.now()
    payload.update(
        {
            "y": now_local.year,
            "m": now_local.month,
            "d": now_local.day,
            "hh": now_local.hour,
            "mm": now_local.minute,
            "ss": now_local.second,
        }
    )
    return payload


class Session:
    def __init__(self, client: BleakClient) -> None:
        self.client = client
        self.refresh_requested = asyncio.Event()

    def _on_refresh(self, _char, _data: bytearray) -> None:
        log("Refresh requested by device")
        self.refresh_requested.set()

    async def setup_refresh_subscription(self) -> None:
        try:
            await self.client.start_notify(REQ_CHAR_UUID, self._on_refresh)
        except (BleakError, ValueError) as e:
            log(f"Refresh subscription unavailable: {e}")

    async def write_payload(self, payload: dict) -> bool:
        data = json.dumps(payload, separators=(",", ":")).encode()
        log(f"Sending: {data.decode()}")
        try:
            await self.client.write_gatt_char(RX_CHAR_UUID, data, response=True)
            return True
        except BleakError as e:
            log(f"Write failed: {e}")
            return False


async def connect_and_run(address: str, stop_event: asyncio.Event) -> bool:
    """Connect to a known address and poll until disconnected or stopped.

    Returns True if the connection was used successfully (so the caller
    keeps the cached address), False if the connection failed and the
    cache should be invalidated.
    """
    log(f"Connecting to {address}...")
    client = BleakClient(address)
    try:
        await client.connect()
    except (BleakError, asyncio.TimeoutError) as e:
        log(f"Connection failed: {e}")
        return False

    if not client.is_connected:
        log("Connection failed (no error but not connected)")
        return False

    log("Connected")
    session = Session(client)
    await session.setup_refresh_subscription()

    last_poll = 0.0
    used_successfully = False
    try:
        while client.is_connected and not stop_event.is_set():
            now = time.time()
            elapsed = now - last_poll
            if session.refresh_requested.is_set() or elapsed >= POLL_INTERVAL:
                session.refresh_requested.clear()
                creds = read_credentials()
                if not creds:
                    log("No credentials; skipping poll")
                else:
                    payload = await poll_api(creds)
                    if payload is not None:
                        if await session.write_payload(payload):
                            last_poll = time.time()
                            used_successfully = True

            try:
                await asyncio.wait_for(session.refresh_requested.wait(), timeout=TICK)
            except asyncio.TimeoutError:
                pass
    finally:
        try:
            await client.disconnect()
        except BleakError:
            pass

    log("Device disconnected" if not stop_event.is_set() else "Stopping")
    return used_successfully


async def main() -> None:
    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()

    def _stop(*_args: object) -> None:
        log("Daemon stopping")
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _stop)
        except NotImplementedError:
            signal.signal(sig, _stop)

    log("=== Claude Usage Tracker Daemon (BLE, macOS) ===")
    log(f"Poll interval: {POLL_INTERVAL}s")

    backoff = 1
    consecutive_scan_misses = 0
    while not stop_event.is_set():
        address = load_cached_address()
        if not address:
            address = await scan_for_device()
            if address:
                consecutive_scan_misses = 0
                save_address(address)
            else:
                consecutive_scan_misses += 1
                log(f"Device not found, retrying in {backoff}s...")
                if consecutive_scan_misses >= MAX_SCAN_MISSES_BEFORE_RESTART:
                    log("Device scan appears stuck; exiting for LaunchAgent restart")
                    sys.exit(2)
                try:
                    await asyncio.wait_for(stop_event.wait(), timeout=backoff)
                except asyncio.TimeoutError:
                    pass
                backoff = min(backoff * 2, MAX_RETRY_BACKOFF)
                continue

        ok = await connect_and_run(address, stop_event)
        if not ok:
            log("Invalidating cached address")
            SAVED_ADDR_FILE.unlink(missing_ok=True)
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=backoff)
            except asyncio.TimeoutError:
                pass
            backoff = min(backoff * 2, MAX_RETRY_BACKOFF)
        else:
            backoff = 1


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
