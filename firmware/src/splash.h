#pragma once
#include <stdint.h>
#include <lvgl.h>
#include "data.h"

// Initialize splash module. Creates the canvas widget inside `parent` and
// allocates the 480x480 pixel buffer (PSRAM).
void splash_init(lv_obj_t *parent);

// Advance animation frame if hold time elapsed. Call from main loop.
void splash_tick(void);

// Cycle to the next animation in the catalog.
void splash_next(void);

// Show/hide the splash container.
void splash_show(void);
void splash_hide(void);

// Pick the next animation matching the current usage-rate group.
// Called automatically by splash_show(); also exposed so other modules can
// trigger a re-pick when the rate group changes mid-display.
void splash_pick_for_current_rate(void);

// Override the animation group the mascot draws from (0=lowest .. 3=highest
// energy). Used by the info panel to drive the pet's mood from weather / air
// quality. Pass -1 to clear the override and fall back to usage_rate_group().
void splash_set_group(int group);

// Update the calendar overlay shown alongside the mascot.
void splash_update_calendar(const UsageData* data);

// Info-panel mode: feed the clock + calendar from an explicit local date-time
// (NTP-backed) instead of a host UsageData payload.
void splash_set_datetime(int year, int month, int day,
                         int hour, int minute, int second);

// True when splash is currently rendering (used to gate re-picks).
bool splash_is_active(void);

// Root container (so ui.cpp can attach a click event).
lv_obj_t* splash_get_root(void);
