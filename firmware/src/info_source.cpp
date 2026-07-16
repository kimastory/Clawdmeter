#include "info_source.h"
#include "net_util.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif

// ---- Config (override in wifi_config.h) ----
#ifndef CLAWDMETER_WIFI_SSID
#define CLAWDMETER_WIFI_SSID ""
#endif
#ifndef CLAWDMETER_WIFI_PASS
#define CLAWDMETER_WIFI_PASS ""
#endif
#ifndef CLAWDMETER_LAT
#define CLAWDMETER_LAT "37.5665"      // Seoul
#endif
#ifndef CLAWDMETER_LON
#define CLAWDMETER_LON "126.9780"
#endif
#ifndef CLAWDMETER_TZ
#define CLAWDMETER_TZ "KST-9"         // POSIX TZ: Korea, no DST
#endif

#define WIFI_RECONNECT_MS 10000UL
#define WEATHER_POLL_MS   600000UL    // 10 min — weather/AQI change slowly
#define FIRST_POLL_DELAY  3000UL      // let WiFi + NTP settle before first fetch

static const char* FORECAST_URL =
    "https://api.open-meteo.com/v1/forecast?latitude=" CLAWDMETER_LAT
    "&longitude=" CLAWDMETER_LON
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code"
    "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
    "&timezone=auto&forecast_days=1";

static const char* AIRQUALITY_URL =
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" CLAWDMETER_LAT
    "&longitude=" CLAWDMETER_LON
    "&current=pm2_5,pm10,us_aqi&timezone=auto";

static InfoData data = {};
static uint32_t wifi_last_connect_ms = 0;
static uint32_t last_poll_ms = 0;
static bool ntp_configured = false;
static int last_second = -1;

static bool wifi_configured(void) {
    return CLAWDMETER_WIFI_SSID[0] != '\0';
}

void info_source_begin(void) {
    if (!wifi_configured()) {
        Serial.println("info: WiFi not configured — set CLAWDMETER_WIFI_SSID in wifi_config.h");
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(CLAWDMETER_WIFI_SSID, CLAWDMETER_WIFI_PASS);
    wifi_last_connect_ms = millis();
    Serial.printf("info: WiFi connecting to %s\n", CLAWDMETER_WIFI_SSID);
}

bool info_source_wifi_connected(void) {
    return WiFi.status() == WL_CONNECTED;
}

// Refresh data.{year..second} from the system clock. Returns true once NTP has
// produced a plausible (post-2020) time.
static bool refresh_clock(void) {
    time_t now = time(nullptr);
    if (now < 1600000000) return false;  // NTP not synced yet
    struct tm tmv;
    localtime_r(&now, &tmv);
    data.year   = tmv.tm_year + 1900;
    data.month  = tmv.tm_mon + 1;
    data.day    = tmv.tm_mday;
    data.hour   = tmv.tm_hour;
    data.minute = tmv.tm_min;
    data.second = tmv.tm_sec;
    data.time_valid = true;
    return true;
}

static void fetch_weather(void) {
    String body;
    if (!https_get(FORECAST_URL, body)) return;

    JsonDocument doc;
    JsonDocument filter;
    filter["current"]["time"] = true;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["current"]["apparent_temperature"] = true;
    filter["current"]["weather_code"] = true;
    filter["daily"]["temperature_2m_max"] = true;
    filter["daily"]["temperature_2m_min"] = true;
    filter["daily"]["precipitation_probability_max"] = true;
    DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("info: weather JSON error: %s\n", err.c_str());
        return;
    }

    JsonObject cur = doc["current"];
    data.temp_c       = cur["temperature_2m"] | 0.0f;
    data.feels_c      = cur["apparent_temperature"] | 0.0f;
    data.humidity     = cur["relative_humidity_2m"] | 0;
    data.weather_code = cur["weather_code"] | -1;

    // Drive the clock/calendar from Open-Meteo's local timestamp
    // (timezone=auto). Robust even when NTP (UDP 123) is blocked; NTP, when it
    // syncs, still takes over per-second precision in info_source_tick().
    const char* ts = cur["time"] | "";
    int y, mo, d, hh, mm;
    if (sscanf(ts, "%d-%d-%dT%d:%d", &y, &mo, &d, &hh, &mm) == 5) {
        data.year = y; data.month = mo; data.day = d;
        data.hour = hh; data.minute = mm; data.second = 0;
        data.time_valid = true;
    }

    JsonObject daily = doc["daily"];
    data.temp_max    = daily["temperature_2m_max"][0] | 0.0f;
    data.temp_min    = daily["temperature_2m_min"][0] | 0.0f;
    data.precip_prob = daily["precipitation_probability_max"][0] | 0;

    data.weather_valid = true;
    Serial.printf("info: weather %.1fC feels %.1fC hum %d%% code %d\n",
                  data.temp_c, data.feels_c, data.humidity, data.weather_code);
}

static void fetch_air_quality(void) {
    String body;
    if (!https_get(AIRQUALITY_URL, body)) return;

    JsonDocument doc;
    JsonDocument filter;
    filter["current"]["pm2_5"] = true;
    filter["current"]["pm10"] = true;
    filter["current"]["us_aqi"] = true;
    DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("info: AQI JSON error: %s\n", err.c_str());
        return;
    }

    JsonObject cur = doc["current"];
    data.pm2_5  = cur["pm2_5"] | 0.0f;
    data.pm10   = cur["pm10"] | 0.0f;
    data.us_aqi = cur["us_aqi"] | -1;
    data.aqi_valid = true;
    Serial.printf("info: AQI us=%d pm2.5=%.1f pm10=%.1f\n",
                  data.us_aqi, data.pm2_5, data.pm10);
}

bool info_source_tick(void) {
    bool changed = false;

    if (!wifi_configured()) {
        // Still advance the clock label even without NTP so the UI isn't dead;
        // time_valid stays false until a real sync happens.
        return false;
    }

    uint32_t now = millis();

    // Maintain WiFi association.
    if (WiFi.status() != WL_CONNECTED) {
        if (now - wifi_last_connect_ms >= WIFI_RECONNECT_MS) {
            Serial.println("info: WiFi reconnecting");
            WiFi.disconnect();
            WiFi.begin(CLAWDMETER_WIFI_SSID, CLAWDMETER_WIFI_PASS);
            wifi_last_connect_ms = now;
        }
        return false;
    }

    // Configure NTP once connected.
    if (!ntp_configured) {
        configTzTime(CLAWDMETER_TZ, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
        ntp_configured = true;
        Serial.println("info: NTP configured");
    }

    // Tick the clock (once per second).
    if (refresh_clock() && data.second != last_second) {
        last_second = data.second;
        changed = true;
    }

    // Poll weather + air quality on a slow cadence (after an initial settle).
    bool due = (last_poll_ms == 0 && now >= FIRST_POLL_DELAY) ||
               (last_poll_ms != 0 && now - last_poll_ms >= WEATHER_POLL_MS);
    if (due) {
        last_poll_ms = now;
        fetch_weather();
        fetch_air_quality();
        changed = true;
    }

    return changed;
}

const InfoData* info_source_data(void) {
    return &data;
}
