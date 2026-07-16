#pragma once

// Copy this file to wifi_config.h (git-ignored) and fill in your values.

// ---- Wi-Fi (required for both WiFi usage transport and the info panel) ----
#define CLAWDMETER_WIFI_SSID "your-ssid"
#define CLAWDMETER_WIFI_PASS "replace-with-wifi-password"

// ---- Usage-over-HTTP transport (AI-usage build only) ----
// Point at a host running the clawdmeter HTTP daemon. Ignored by the
// info-panel build (-DCLAWDMETER_INFO_PANEL), which fetches from the internet
// directly and needs no host.
#define CLAWDMETER_USAGE_URL "http://192.168.1.204:8787/usage"

// ---- Info panel (used only when built with -DCLAWDMETER_INFO_PANEL) ----
// Location for weather + air quality (Open-Meteo) and POSIX timezone for the
// clock. Defaults to Seoul / KST if omitted.
#define CLAWDMETER_LAT "37.5665"
#define CLAWDMETER_LON "126.9780"
#define CLAWDMETER_TZ  "KST-9"     // POSIX TZ, e.g. "PST8PDT", "CET-1CEST"

// ---- Info panel: stock screen (used only when built with -DCLAWDMETER_INFO_PANEL) ----
// Ticker defaults to Samsung Electronics (005930.KS / KOSPI). Override the
// symbol/name to track a different stock; avg-buy price drives the
// gain/loss line and has no sane default, so set it to your own.
// #define CLAWDMETER_STOCK_SYMBOL "005930.KS"
// #define CLAWDMETER_STOCK_NAME   "삼성전자"
#define CLAWDMETER_STOCK_AVG_BUY 0
