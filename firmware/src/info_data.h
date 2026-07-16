#pragma once
#include <Arduino.h>

// Data model for the standalone WiFi info panel (no host, no BLE).
// Populated by info_source from NTP (time) + Open-Meteo (weather, air quality).
struct InfoData {
    // ---- Local time (NTP-backed) ----
    int  year;          // e.g. 2026
    int  month;         // 1-12
    int  day;           // 1-31
    int  hour;          // 0-23
    int  minute;        // 0-59
    int  second;        // 0-59
    bool time_valid;    // true once NTP has synced at least once

    // ---- Current weather (Open-Meteo forecast) ----
    float temp_c;       // temperature_2m
    float feels_c;      // apparent_temperature
    float temp_max;     // daily max
    float temp_min;     // daily min
    int   humidity;     // relative_humidity_2m, %
    int   weather_code; // WMO weather interpretation code
    int   precip_prob;  // daily precipitation_probability_max, %
    bool  weather_valid;

    // ---- Air quality (Open-Meteo air-quality) ----
    float pm2_5;        // µg/m³
    float pm10;         // µg/m³
    int   us_aqi;       // US AQI (0-500+)
    bool  aqi_valid;
};
