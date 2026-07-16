#include "stock_source.h"
#include "net_util.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif

// ---- Config (override in wifi_config.h) ----
#ifndef CLAWDMETER_STOCK_SYMBOL
#define CLAWDMETER_STOCK_SYMBOL "005930.KS"   // Samsung Electronics (KOSPI)
#endif
#ifndef CLAWDMETER_STOCK_NAME
#define CLAWDMETER_STOCK_NAME "삼성전자"
#endif
#ifndef CLAWDMETER_STOCK_AVG_BUY
#define CLAWDMETER_STOCK_AVG_BUY 0
#endif

#define STOCK_POLL_MS     60000UL   // 1 min — plenty for a toy ticker
#define STOCK_FIRST_DELAY 5000UL    // let WiFi settle before the first fetch

// 3 months of daily closes — a proper trend view, month-labelled on the x-axis.
static const char* CHART_URL =
    "https://query1.finance.yahoo.com/v8/finance/chart/" CLAWDMETER_STOCK_SYMBOL
    "?interval=1d&range=3mo";

static StockData data = {};
static uint32_t  last_poll_ms = 0;

static int month_of(long epoch) {
    time_t t = (time_t)epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    return tmv.tm_mon + 1;
}

static void fetch_stock(void) {
    String body;
    if (!https_get(CHART_URL, body)) return;

    JsonDocument doc;
    JsonDocument filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["timestamp"] = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;
    DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("stock: JSON error: %s\n", err.c_str());
        return;
    }

    JsonObject result = doc["chart"]["result"][0];
    if (result.isNull()) {
        Serial.println("stock: no result in response");
        return;
    }

    strlcpy(data.name, CLAWDMETER_STOCK_NAME, sizeof(data.name));
    data.price         = lroundf(result["meta"]["regularMarketPrice"] | 0.0f);
    data.avg_buy_price = CLAWDMETER_STOCK_AVG_BUY;

    JsonArray closes = result["indicators"]["quote"][0]["close"];
    JsonArray times  = result["timestamp"];
    int n = closes.size();
    bool have_times = (int)times.size() >= n;
    int start = (n > STOCK_HISTORY_MAX) ? n - STOCK_HISTORY_MAX : 0;
    int count = 0;
    int32_t last_val = (int32_t)data.price;
    int prev_month = -1;
    int mcount = 0;
    for (int i = start; i < n; i++) {
        JsonVariant v = closes[i];
        if (!v.isNull()) last_val = lroundf(v.as<float>());
        data.history[count] = last_val;   // carry forward over market-closed gaps

        if (have_times) {
            int mon = month_of(times[i] | 0L);
            if (mon != prev_month && mcount < STOCK_MONTH_MARKS_MAX) {
                data.month_mark_idx[mcount] = count;
                data.month_mark_val[mcount] = mon;
                mcount++;
                prev_month = mon;
            }
        }
        count++;
    }
    data.history_count = count;
    data.month_mark_count = mcount;
    data.valid = true;

    Serial.printf("stock: %s price=%ld history=%d months=%d\n",
                  data.name, data.price, data.history_count, data.month_mark_count);
}

bool stock_source_tick(void) {
    if (WiFi.status() != WL_CONNECTED) return false;

    uint32_t now = millis();
    bool due = (last_poll_ms == 0 && now >= STOCK_FIRST_DELAY) ||
               (last_poll_ms != 0 && now - last_poll_ms >= STOCK_POLL_MS);
    if (!due) return false;

    last_poll_ms = now;
    fetch_stock();
    return true;
}

const StockData* stock_source_data(void) {
    return &data;
}
