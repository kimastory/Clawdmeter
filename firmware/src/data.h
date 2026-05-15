#pragma once
#include <Arduino.h>

struct UsageData {
    float session_pct;       // 5-hour window utilization (0-100)
    int session_reset_mins;  // minutes until session resets
    float weekly_pct;        // 7-day window utilization (0-100)
    int weekly_reset_mins;   // minutes until weekly resets
    char status[16];         // "allowed" or "limited"
    int year;                // local calendar year from host
    int month;               // local calendar month (1-12)
    int day;                 // local calendar day (1-31)
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
};
