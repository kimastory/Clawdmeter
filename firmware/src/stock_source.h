#pragma once
#include "stock_data.h"

// Standalone stock quote + trend source (Yahoo Finance chart API). Piggybacks
// on the WiFi connection brought up by info_source — no separate begin().

// Call every loop once WiFi is up. Polls on a slow cadence. Returns true when
// new data was fetched this tick so the caller can refresh the UI.
bool stock_source_tick(void);

// Latest snapshot. Always non-null; check `valid` before trusting it.
const StockData* stock_source_data(void);
