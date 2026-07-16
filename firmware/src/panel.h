#pragma once
#include "info_data.h"
#include "stock_data.h"

// Standalone WiFi info-panel UI. Manages three auto-cycling screens:
//   CLOCK  — dog+cat companions + big clock + calendar
//   INFO   — combined weather + air quality
//   STOCK  — ticker name, live price, trend chart, gain/loss vs avg-buy price
// No usage, no BLE. Build with -DCLAWDMETER_INFO_PANEL.

void panel_init(void);                    // build all screens
void panel_update(const InfoData* d);     // push latest weather/AQI/time data
void panel_update_stock(const StockData* d); // push latest stock data
void panel_tick(void);                    // advance auto-cycle
void panel_next_screen(void);             // manual advance (middle button)
void panel_pet_next(void);                // cycle mascot animation (side button)
void panel_show_index(int i);             // jump to screen 0=clock 1=info 2=stock (QA)
