#pragma once
#include "info_data.h"

// Standalone data source for the WiFi info panel. Owns WiFi bring-up, NTP time
// sync, and periodic polling of the Open-Meteo weather + air-quality APIs.
// No host daemon, no BLE — the device talks to the internet directly.

// Bring up WiFi (STA) and configure NTP. Safe to call once from setup().
void info_source_begin(void);

// Call every loop. Keeps the local clock fresh from the RTC/NTP and polls the
// weather + air-quality endpoints on a slow interval when WiFi is connected.
// Returns true when the underlying InfoData changed this tick (new fetch or a
// new second on the clock) so the caller can refresh the UI.
bool info_source_tick(void);

// Latest snapshot. Always non-null; check the *_valid flags before trusting
// individual sections.
const InfoData* info_source_data(void);

// True while WiFi is associated.
bool info_source_wifi_connected(void);
