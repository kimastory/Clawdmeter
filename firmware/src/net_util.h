#pragma once
#include <Arduino.h>

// Shared HTTPS GET helper (cert validation skipped — hobby device). Used by
// every info-panel data source (weather, air quality, stock).
bool https_get(const char* url, String& body, uint32_t timeout_ms = 8000);
